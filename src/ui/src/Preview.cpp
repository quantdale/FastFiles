#include "Preview.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cwctype>
#include <initializer_list>
#include <thread>
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

namespace ffui {
namespace {

std::wstring ExtensionOf(const std::wstring& path) {
    const size_t dot = path.find_last_of(L'.');
    const size_t slash = path.find_last_of(L"\\/");
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) {
        return L"";
    }
    std::wstring extension = path.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(towlower(character));
    });
    return extension;
}

bool HasExtension(const std::wstring& path, std::initializer_list<const wchar_t*> extensions) {
    const std::wstring extension = ExtensionOf(path);
    return std::any_of(extensions.begin(), extensions.end(), [&extension](const wchar_t* candidate) {
        return extension == candidate;
    });
}

// ui-v3 preview: map a Win32 open/read error onto a deliberate preview state.
// Access denied is its own state so the pane can explain the difference
// between "not allowed" and "not there anymore".
PreviewState StateForFileError(DWORD error) {
    if (error == ERROR_ACCESS_DENIED) {
        return PreviewState::PermissionDenied;
    }
    return PreviewState::Unavailable;
}

class ImagePreviewProvider final : public IPreviewProvider {
public:
    MatchResult GetPriority(const FileDescriptor& descriptor) const override {
        return HasExtension(descriptor.path, {L".jpg", L".jpeg", L".png", L".bmp", L".gif", L".tiff"})
            ? MatchResult::ExtensionMatch : MatchResult::NoMatch;
    }

    PreviewResult CreatePreview(const FileDescriptor& descriptor, const CancellationToken& cancellation) const override {
        if (cancellation.IsCancelled()) {
            return {};
        }
        Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) {
            return {};
        }
        Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
        const HRESULT openResult = factory->CreateDecoderFromFilename(descriptor.path.c_str(), nullptr, GENERIC_READ,
                                                                      WICDecodeMetadataCacheOnDemand, &decoder);
        if (FAILED(openResult)) {
            // ui-v3 preview: denied opens are a distinct pane state; every
            // other decode-open failure is unsupported/corrupt image data.
            PreviewResult failure;
            failure.state =
                HRESULT_FACILITY(openResult) == FACILITY_WIN32 && HRESULT_CODE(openResult) == ERROR_ACCESS_DENIED
                    ? PreviewState::PermissionDenied : PreviewState::Unsupported;
            return failure;
        }
        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(0, &frame))) {
            return {};
        }
        UINT width = 0;
        UINT height = 0;
        if (FAILED(frame->GetSize(&width, &height))) {
            width = 0;
            height = 0;
        }
        if (width > 16384 || height > 16384) {
            // ui-v3 preview: oversized frames get the deliberate too-large
            // state instead of a bare no-content result.
            PreviewResult tooLarge;
            tooLarge.state = PreviewState::TooLarge;
            return tooLarge;
        }
        if (width == 0 || height == 0) {
            return {};
        }
        Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
        if (FAILED(factory->CreateFormatConverter(&converter)) ||
            FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                                          nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
            return {};
        }
        const uint64_t byteCount = static_cast<uint64_t>(width) * height * 4;
        if (cancellation.IsCancelled()) {
            return {};
        }
        if (byteCount > 64ull * 1024 * 1024) {
            PreviewResult tooLarge;
            tooLarge.state = PreviewState::TooLarge;
            return tooLarge;
        }
        PreviewResult result;
        result.kind = PreviewKind::Image;
        result.width = width;
        result.height = height;
        result.pixels.resize(static_cast<size_t>(byteCount));
        if (FAILED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(byteCount), result.pixels.data())) ||
            cancellation.IsCancelled()) {
            return {};
        }
        return result;
    }
};

class TextPreviewProvider final : public IPreviewProvider {
public:
    MatchResult GetPriority(const FileDescriptor& descriptor) const override {
        return HasExtension(descriptor.path, {L".txt", L".log", L".md", L".json", L".xml", L".yaml", L".yml",
                                               L".cpp", L".c", L".h", L".hpp", L".cs", L".py", L".js", L".ts",
                                               L".html", L".css", L".cmake"}) ? MatchResult::ExtensionMatch : MatchResult::NoMatch;
    }

    PreviewResult CreatePreview(const FileDescriptor& descriptor, const CancellationToken& cancellation) const override {
        // The shell forwards the user-configured Preview Limit in
        // descriptor.maxBytes; 0 keeps the historical 256 KB default.
        constexpr DWORD kDefaultByteCeiling = 256 * 1024;
        constexpr uint64_t kMaxByteCeiling = 64ull * 1024 * 1024;
        const DWORD kByteCeiling = descriptor.maxBytes > 0 && descriptor.maxBytes <= kMaxByteCeiling
            ? static_cast<DWORD>(descriptor.maxBytes) : kDefaultByteCeiling;
        HANDLE file = CreateFileW(descriptor.path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            // ui-v3 preview: denied/missing/locked opens resolve to distinct
            // deliberate states instead of an unexplained empty pane.
            PreviewResult failure;
            failure.state = StateForFileError(GetLastError());
            return failure;
        }
        LARGE_INTEGER size{};
        const bool gotSize = GetFileSizeEx(file, &size) != FALSE;
        const DWORD bytesToRead = gotSize && size.QuadPart > kByteCeiling ? kByteCeiling :
            static_cast<DWORD>(gotSize ? size.QuadPart : kByteCeiling);
        std::vector<char> bytes(bytesToRead);
        DWORD read = 0;
        const BOOL ok = ReadFile(file, bytes.data(), bytesToRead, &read, nullptr);
        CloseHandle(file);
        if (!ok || cancellation.IsCancelled()) {
            if (!ok) {
                PreviewResult failure;
                failure.state = StateForFileError(GetLastError());
                return failure;
            }
            return {};
        }
        size_t offset = 0;
        if (read >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF && static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF) {
            offset = 3;
        } else if (read >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF && static_cast<unsigned char>(bytes[1]) == 0xFE) {
            PreviewResult result;
            result.kind = PreviewKind::Text;
            const size_t wcharCount = (read - 2) / sizeof(wchar_t);
            result.text.resize(wcharCount);
            if (wcharCount > 0) {
                // Avoid unaligned wchar_t* reinterpretation of the byte buffer.
                std::memcpy(result.text.data(), bytes.data() + 2, wcharCount * sizeof(wchar_t));
            }
            LimitLines(result, gotSize && size.QuadPart > kByteCeiling);
            return result;
        } else if (read >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFE && static_cast<unsigned char>(bytes[1]) == 0xFF) {
            PreviewResult result;
            result.kind = PreviewKind::Text;
            for (DWORD index = 2; index + 1 < read; index += 2) {
                const wchar_t character = static_cast<wchar_t>((static_cast<unsigned char>(bytes[index]) << 8) |
                                                                static_cast<unsigned char>(bytes[index + 1]));
                result.text.push_back(character);
            }
            LimitLines(result, gotSize && size.QuadPart > kByteCeiling);
            return result;
        }
        // BOM-less text: prefer UTF-8 when the bytes form valid UTF-8
        // sequences (the overwhelming default for source/markdown files);
        // only fall back to the ANSI code page when they do not, so BOM-less
        // UTF-8 no longer renders as mojibake on Western-locale machines.
        UINT codePage = CP_UTF8;
        DWORD conversionFlags = MB_ERR_INVALID_CHARS;
        int characterCount = MultiByteToWideChar(codePage, conversionFlags, bytes.data() + offset,
                                                 static_cast<int>(read - offset), nullptr, 0);
        if (characterCount <= 0) {
            codePage = GetACP();
            conversionFlags = 0;
            characterCount = MultiByteToWideChar(codePage, conversionFlags, bytes.data() + offset,
                                                 static_cast<int>(read - offset), nullptr, 0);
        }
        if (characterCount <= 0) {
            return {};
        }
        PreviewResult result;
        result.kind = PreviewKind::Text;
        result.text.resize(characterCount);
        MultiByteToWideChar(codePage, conversionFlags, bytes.data() + offset,
                            static_cast<int>(read - offset), result.text.data(), characterCount);
        LimitLines(result, gotSize && size.QuadPart > kByteCeiling);
        return result;
    }

private:
    static void LimitLines(PreviewResult& result, bool byteTruncated) {
        constexpr size_t kLineCeiling = 4000;
        size_t lines = 0;
        for (size_t index = 0; index < result.text.size(); ++index) {
            if (result.text[index] == L'\n' && ++lines >= kLineCeiling) {
                result.text.resize(index + 1);
                result.truncated = true;
                return;
            }
        }
        result.truncated = byteTruncated;
    }
};

} // namespace

void PreviewProviderRegistry::Register(std::unique_ptr<IPreviewProvider> provider) {
    providers_.push_back(std::move(provider));
}

PreviewResult PreviewProviderRegistry::CreatePreview(const FileDescriptor& descriptor, const CancellationToken& cancellation) const {
    for (MatchResult pass : {MatchResult::ExtensionMatch, MatchResult::ContentSniffMatch}) {
        for (const auto& provider : providers_) {
            if (provider->GetPriority(descriptor) != pass) {
                continue;
            }
            try {
                PreviewResult result = provider->CreatePreview(descriptor, cancellation);
                if (result.kind != PreviewKind::None || cancellation.IsCancelled()) {
                    // A provider returning content has completed successfully.
                    // Keep the user-visible state consistent even when a new
                    // provider only fills in its payload kind and text/pixels;
                    // PreviewResult defaults to Unsupported for no-content
                    // fallthrough, so leaving it untouched would make a real
                    // preview render while the shell advertises failure.
                    if (result.kind != PreviewKind::None) result.state = PreviewState::Ready;
                    return result;
                }
                // ui-v3 preview: an identified failure (denied, too large,
                // unavailable) is terminal — no other provider can do better
                // with the same file. Only the generic unsupported fallthrough
                // keeps trying remaining candidates.
                if (result.state != PreviewState::Unsupported) {
                    return result;
                }
            } catch (...) {
                // A malformed file must not take down the preview pane.
            }
        }
    }
    // Nothing produced content and no provider identified a sharper cause:
    // the deliberate pane state is "unsupported type".
    return {};
}

std::unique_ptr<IPreviewProvider> CreateImagePreviewProvider() { return std::make_unique<ImagePreviewProvider>(); }
std::unique_ptr<IPreviewProvider> CreateTextPreviewProvider() { return std::make_unique<TextPreviewProvider>(); }

PreviewController::PreviewController(Completion completion) : completion_(std::move(completion)) {
    registry_.Register(CreateImagePreviewProvider());
    registry_.Register(CreateTextPreviewProvider());
    workers_.emplace_back(&PreviewController::WorkerLoop, this);
    workers_.emplace_back(&PreviewController::WorkerLoop, this);
}

PreviewController::~PreviewController() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        if (cancellation_) {
            cancellation_->store(true);
        }
        pendingWork_.clear();
    }
    workAvailable_.notify_all();
    for (std::thread& worker : workers_) {
        worker.join();
    }
}

void PreviewController::WorkerLoop() {
    for (;;) {
        std::function<void()> work;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            workAvailable_.wait(lock, [this] { return stopping_ || !pendingWork_.empty(); });
            if (stopping_) {
                return;
            }
            work = std::move(pendingWork_.front());
            pendingWork_.pop_front();
        }
        work();
    }
}

uint64_t PreviewController::Request(const FileDescriptor& descriptor) {
    std::shared_ptr<std::atomic<bool>> cancellation = std::make_shared<std::atomic<bool>>(false);
    uint64_t requestId = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (cancellation_) {
            cancellation_->store(true);
        }
        cancellation_ = cancellation;
        requestId = ++requestId_;
        // A newer selection supersedes every queued preview. Keep only the
        // latest work item; already-running workers still observe their
        // cancellation token and perform the final request-identity check
        // before invoking the UI callback.
        pendingWork_.clear();
        pendingWork_.push_back([this, descriptor, cancellation, requestId] {
        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        PreviewResult result = registry_.CreatePreview(descriptor, CancellationToken(cancellation));
        if (SUCCEEDED(comResult)) {
            CoUninitialize();
        }
        // Give a rapid selection burst a short coalescing window. Without
        // this grace period a very fast file can complete between two UI
        // selection messages and briefly publish an obsolete preview even
        // though a newer request is already queued by the caller.
        for (int delay = 0; delay < 25 && !cancellation->load(); ++delay) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        bool stillCurrent = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stillCurrent = !stopping_ && cancellation_ == cancellation && requestId_ == requestId;
        }
        if (stillCurrent && !cancellation->load()) {
            completion_(requestId, std::move(result));
        }
        });
    }
    workAvailable_.notify_one();
    return requestId;
}

void PreviewController::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (cancellation_) {
        cancellation_->store(true);
    }
    ++requestId_;
    pendingWork_.clear();
}

} // namespace ffui
