#include "Preview.h"

#include <algorithm>
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
        if (FAILED(factory->CreateDecoderFromFilename(descriptor.path.c_str(), nullptr, GENERIC_READ,
                                                       WICDecodeMetadataCacheOnDemand, &decoder))) {
            return {};
        }
        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(0, &frame))) {
            return {};
        }
        UINT width = 0;
        UINT height = 0;
        if (FAILED(frame->GetSize(&width, &height)) || width == 0 || height == 0 || width > 16384 || height > 16384) {
            return {};
        }
        Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
        if (FAILED(factory->CreateFormatConverter(&converter)) ||
            FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                                          nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
            return {};
        }
        const uint64_t byteCount = static_cast<uint64_t>(width) * height * 4;
        if (byteCount > 64ull * 1024 * 1024 || cancellation.IsCancelled()) {
            return {};
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
        constexpr DWORD kByteCeiling = 256 * 1024;
        HANDLE file = CreateFileW(descriptor.path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            return {};
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
            return {};
        }
        UINT codePage = GetACP();
        size_t offset = 0;
        if (read >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF && static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF) {
            codePage = CP_UTF8;
            offset = 3;
        } else if (read >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF && static_cast<unsigned char>(bytes[1]) == 0xFE) {
            PreviewResult result;
            result.kind = PreviewKind::Text;
            result.text.assign(reinterpret_cast<const wchar_t*>(bytes.data() + 2), (read - 2) / sizeof(wchar_t));
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
        const int characterCount = MultiByteToWideChar(codePage, 0, bytes.data() + offset, static_cast<int>(read - offset), nullptr, 0);
        if (characterCount <= 0) {
            return {};
        }
        PreviewResult result;
        result.kind = PreviewKind::Text;
        result.text.resize(characterCount);
        MultiByteToWideChar(codePage, 0, bytes.data() + offset, static_cast<int>(read - offset), result.text.data(), characterCount);
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
                    return result;
                }
            } catch (...) {
                // A malformed file must not take down the preview pane.
            }
        }
    }
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
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingWork_.push_back([this, descriptor, cancellation, requestId] {
        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        PreviewResult result = registry_.CreatePreview(descriptor, CancellationToken(cancellation));
        if (SUCCEEDED(comResult)) {
            CoUninitialize();
        }
        if (!cancellation->load()) {
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
}

} // namespace ffui
