#include "NewFileWriter.h"
#include "../TestSupport.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <wrl/client.h>

namespace {
class TestStream final : public IStream {
public:
    enum class Mode { Normal, ReadFailure, OversizedRead, SeekFailure };
    explicit TestStream(Mode mode) : mode_(mode) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (object == nullptr) return E_POINTER;
        *object = nullptr;
        if (iid != IID_IUnknown && iid != IID_ISequentialStream && iid != IID_IStream) return E_NOINTERFACE;
        *object = static_cast<IStream*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG result = --references_;
        if (result == 0) delete this;
        return result;
    }
    HRESULT STDMETHODCALLTYPE Read(void* buffer, ULONG count, ULONG* read) override {
        *read = 0;
        if (mode_ == Mode::OversizedRead) { *read = count + 1; return S_OK; }
        if (mode_ == Mode::ReadFailure && offset_ != 0) return STG_E_READFAULT;
        const auto available = static_cast<ULONG>((std::min)(size_t{12000} - offset_, static_cast<size_t>(count)));
        std::memset(buffer, 'x', available); offset_ += available; *read = available;
        return available < count ? S_FALSE : S_OK;
    }
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER*) override {
        if (mode_ == Mode::SeekFailure) return STG_E_SEEKERROR;
        if (move.QuadPart != 0 || origin != STREAM_SEEK_SET) return E_NOTIMPL;
        offset_ = 0; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Write(const void*, ULONG, ULONG*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*, ULARGE_INTEGER*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Revert() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Stat(STATSTG*, DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Clone(IStream**) override { return E_NOTIMPL; }
private:
    ULONG references_ = 1;
    size_t offset_ = 0;
    Mode mode_;
};
std::string Read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
}

int main() {
    using fftest::Check;
    namespace fs = std::filesystem;
    GUID identity{};
    const HRESULT identityResult = CoCreateGuid(&identity);
    Check(SUCCEEDED(identityResult), "owned fixture identity generated");
    if (FAILED(identityResult)) return fftest::FailureCount();
    wchar_t identityText[40]{};
    Check(StringFromGUID2(identity, identityText, 40) != 0, "fixture identity formatted");
    const auto parent = fs::temp_directory_path();
    const auto root = parent / (std::wstring(L"FastFiles-new-file-") + identityText);
    const bool created = fs::create_directory(root);
    Check(created, "unique owned fixture created");
    if (!created) return fftest::FailureCount();
    Microsoft::WRL::ComPtr<IStream> stream;
    stream.Attach(new TestStream(TestStream::Mode::Normal));
    const auto destination = root / L"destination.lnk";
    auto result = ffui::WriteNewFile(destination.wstring(), stream.Get());
    Check(SUCCEEDED(result.error) && SUCCEEDED(result.cleanupError) && Read(destination) == std::string(12000, 'x'),
          "multi-chunk and final short read create exact bytes");
    result = ffui::WriteNewFile(destination.wstring(), stream.Get());
    Check(result.error == HRESULT_FROM_WIN32(ERROR_FILE_EXISTS) && Read(destination) == std::string(12000, 'x'),
          "existing destination remains byte-identical");
    const HANDLE locked = CreateFileW(destination.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    Check(locked != INVALID_HANDLE_VALUE, "own existing fixture locked");
    result = ffui::WriteNewFile(destination.wstring(), stream.Get());
    Check(FAILED(result.error), "locked existing destination rejected");
    if (locked != INVALID_HANDLE_VALUE) Check(CloseHandle(locked) != FALSE, "own lock closed");
    Check(Read(destination) == std::string(12000, 'x'), "locked existing bytes preserved");
    int index = 0;
    for (const auto mode : {TestStream::Mode::ReadFailure, TestStream::Mode::OversizedRead, TestStream::Mode::SeekFailure}) {
        stream.Attach(new TestStream(mode));
        const auto failedPath = root / (L"failed-" + std::to_wstring(index++));
        result = ffui::WriteNewFile(failedPath.wstring(), stream.Get());
        Check(FAILED(result.error) && SUCCEEDED(result.cleanupError) && !fs::exists(failedPath),
              "failed or malformed stream leaves no owned partial file");
    }
    Check(ffui::WriteNewFile((root / L"null").wstring(), nullptr).error == E_POINTER,
          "null stream rejected before creating destination");
    const std::wstring extendedRoot = L"\\\\?\\" + root.wstring();
    std::wstring deep = extendedRoot;
    for (int depth = 0; depth < 6; ++depth) {
        deep += L"\\" + std::wstring(45, L'd');
        Check(CreateDirectoryW(deep.c_str(), nullptr) != FALSE, "owned long-path directory created");
    }
    stream.Attach(new TestStream(TestStream::Mode::Normal));
    result = ffui::WriteNewFile(deep.substr(4) + L"\\long.lnk", stream.Get());
    Check(SUCCEEDED(result.error) && GetFileAttributesW((deep + L"\\long.lnk").c_str()) != INVALID_FILE_ATTRIBUTES,
          "unprefixed long destination written through extended path");
    Check(fs::equivalent(root.parent_path(), parent), "cleanup remains within own temporary parent");
    std::error_code cleanup;
    if (fs::equivalent(root.parent_path(), parent)) fs::remove_all(fs::path(extendedRoot), cleanup);
    Check(!cleanup && !fs::exists(root), "all owned fixture files cleaned normally");
    return fftest::FailureCount();
}
