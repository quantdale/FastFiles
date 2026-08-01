#include "OleDragDrop.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <shellapi.h>
#include <shlobj_core.h>
#include <shobjidl.h>
#include <wrl/client.h>

namespace ffui {

namespace {

FORMATETC DropFormat() {
    return {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
}

std::vector<std::wstring> ExtractPaths(IDataObject* object) {
    std::vector<std::wstring> paths;
    FORMATETC format = DropFormat();
    STGMEDIUM medium{};
    if (object != nullptr && SUCCEEDED(object->GetData(&format, &medium))) {
        const HDROP drop = static_cast<HDROP>(GlobalLock(medium.hGlobal));
        if (drop != nullptr) {
            const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            for (UINT index = 0; index < count; ++index) {
                const UINT length = DragQueryFileW(drop, index, nullptr, 0);
                std::wstring path(length + 1, L'\0');
                DragQueryFileW(drop, index, path.data(), length + 1);
                path.resize(length);
                paths.push_back(std::move(path));
            }
            GlobalUnlock(medium.hGlobal);
        }
        ReleaseStgMedium(&medium);
        return paths;
    }
    Microsoft::WRL::ComPtr<IShellItemArray> items;
    if (object != nullptr && SUCCEEDED(SHCreateShellItemArrayFromDataObject(object, IID_PPV_ARGS(&items)))) {
        DWORD count = 0;
        items->GetCount(&count);
        for (DWORD index = 0; index < count; ++index) {
            Microsoft::WRL::ComPtr<IShellItem> item;
            PWSTR path = nullptr;
            if (SUCCEEDED(items->GetItemAt(index, &item)) &&
                SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                paths.emplace_back(path);
                CoTaskMemFree(path);
            }
        }
    }
    return paths;
}

class FileDataObject final : public IDataObject {
public:
    explicit FileDataObject(std::vector<std::wstring> paths) : paths_(std::move(paths)) {}
    IFACEMETHODIMP QueryInterface(REFIID iid, void** value) override {
        if (value == nullptr) return E_POINTER;
        if (iid == IID_IUnknown || iid == IID_IDataObject) {
            *value = static_cast<IDataObject*>(this);
            AddRef();
            return S_OK;
        }
        *value = nullptr;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++references_; }
    IFACEMETHODIMP_(ULONG) Release() override {
        const ULONG value = --references_;
        if (value == 0) delete this;
        return value;
    }
    IFACEMETHODIMP GetData(FORMATETC* format, STGMEDIUM* medium) override {
        if (format == nullptr || medium == nullptr) return E_POINTER;
        if (FAILED(QueryGetData(format))) return DV_E_FORMATETC;
        size_t characterCount = 1;
        for (const auto& path : paths_) characterCount += path.size() + 1;
        const SIZE_T byteCount = sizeof(DROPFILES) + characterCount * sizeof(wchar_t);
        HGLOBAL storage = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, byteCount);
        if (storage == nullptr) return E_OUTOFMEMORY;
        auto* header = static_cast<DROPFILES*>(GlobalLock(storage));
        if (header == nullptr) {
            GlobalFree(storage);
            return E_OUTOFMEMORY;
        }
        header->pFiles = sizeof(DROPFILES);
        header->fWide = TRUE;
        auto* output = reinterpret_cast<wchar_t*>(reinterpret_cast<unsigned char*>(header) + sizeof(DROPFILES));
        for (const auto& path : paths_) {
            std::copy(path.begin(), path.end(), output);
            output += path.size();
            *output++ = L'\0';
        }
        *output = L'\0';
        GlobalUnlock(storage);
        medium->tymed = TYMED_HGLOBAL;
        medium->hGlobal = storage;
        medium->pUnkForRelease = nullptr;
        return S_OK;
    }
    IFACEMETHODIMP GetDataHere(FORMATETC*, STGMEDIUM*) override { return DATA_E_FORMATETC; }
    IFACEMETHODIMP QueryGetData(FORMATETC* format) override {
        if (format == nullptr) return E_POINTER;
        return format->cfFormat == CF_HDROP && (format->tymed & TYMED_HGLOBAL) != 0 &&
               format->dwAspect == DVASPECT_CONTENT ? S_OK : DV_E_FORMATETC;
    }
    IFACEMETHODIMP GetCanonicalFormatEtc(FORMATETC*, FORMATETC* output) override {
        if (output != nullptr) output->ptd = nullptr;
        return E_NOTIMPL;
    }
    IFACEMETHODIMP SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
    IFACEMETHODIMP EnumFormatEtc(DWORD direction, IEnumFORMATETC** enumerator) override {
        if (enumerator == nullptr) return E_POINTER;
        *enumerator = nullptr;
        if (direction != DATADIR_GET) return E_NOTIMPL;
        FORMATETC format = DropFormat();
        return SHCreateStdEnumFmtEtc(1, &format, enumerator);
    }
    IFACEMETHODIMP DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
    IFACEMETHODIMP DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    IFACEMETHODIMP EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }

private:
    std::atomic<ULONG> references_{1};
    std::vector<std::wstring> paths_;
};

class FileDropSource final : public IDropSource {
public:
    IFACEMETHODIMP QueryInterface(REFIID iid, void** value) override {
        if (value == nullptr) return E_POINTER;
        if (iid == IID_IUnknown || iid == IID_IDropSource) {
            *value = static_cast<IDropSource*>(this);
            AddRef();
            return S_OK;
        }
        *value = nullptr;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++references_; }
    IFACEMETHODIMP_(ULONG) Release() override {
        const ULONG value = --references_;
        if (value == 0) delete this;
        return value;
    }
    IFACEMETHODIMP QueryContinueDrag(BOOL escapePressed, DWORD keyState) override {
        if (escapePressed) return DRAGDROP_S_CANCEL;
        if ((keyState & MK_LBUTTON) == 0) return DRAGDROP_S_DROP;
        return S_OK;
    }
    IFACEMETHODIMP GiveFeedback(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }
private:
    std::atomic<ULONG> references_{1};
};

class FileDropTarget final : public IDropTarget {
public:
    FileDropTarget(DropDestinationProvider destination, FileDropHandler handler)
        : destination_(std::move(destination)), handler_(std::move(handler)) {}
    IFACEMETHODIMP QueryInterface(REFIID iid, void** value) override {
        if (value == nullptr) return E_POINTER;
        if (iid == IID_IUnknown || iid == IID_IDropTarget) {
            *value = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        *value = nullptr;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++references_; }
    IFACEMETHODIMP_(ULONG) Release() override {
        const ULONG value = --references_;
        if (value == 0) delete this;
        return value;
    }
    IFACEMETHODIMP DragEnter(IDataObject* object, DWORD keys, POINTL, DWORD* effect) override {
        if (effect == nullptr) return E_POINTER;
        paths_ = ExtractPaths(object);
        *effect = DetermineDropEffect(keys, *effect, paths_, destination_());
        return S_OK;
    }
    IFACEMETHODIMP DragOver(DWORD keys, POINTL, DWORD* effect) override {
        if (effect == nullptr) return E_POINTER;
        *effect = DetermineDropEffect(keys, *effect, paths_, destination_());
        return S_OK;
    }
    IFACEMETHODIMP DragLeave() override {
        paths_.clear();
        return S_OK;
    }
    IFACEMETHODIMP Drop(IDataObject* object, DWORD keys, POINTL, DWORD* effect) override {
        if (effect == nullptr) return E_POINTER;
        auto paths = ExtractPaths(object);
        const DWORD chosen = DetermineDropEffect(keys, *effect, paths, destination_());
        *effect = chosen;
        if (chosen != DROPEFFECT_NONE && !paths.empty()) handler_(std::move(paths), chosen);
        paths_.clear();
        return S_OK;
    }
private:
    std::atomic<ULONG> references_{1};
    DropDestinationProvider destination_;
    FileDropHandler handler_;
    std::vector<std::wstring> paths_;
};

} // namespace

HRESULT CreateFileDataObject(const std::vector<std::wstring>& paths, IDataObject** object) {
    if (object == nullptr) return E_POINTER;
    *object = nullptr;
    if (paths.empty()) return E_INVALIDARG;
    *object = new FileDataObject(paths);
    return S_OK;
}

DWORD BeginFileDrag(const std::vector<std::wstring>& paths) {
    IDataObject* object = nullptr;
    if (FAILED(CreateFileDataObject(paths, &object))) return DROPEFFECT_NONE;
    auto* source = new FileDropSource();
    DWORD effect = DROPEFFECT_NONE;
    DoDragDrop(object, source, DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK, &effect);
    source->Release();
    object->Release();
    return effect;
}

DWORD DetermineDropEffect(DWORD keyState, DWORD allowedEffects, const std::vector<std::wstring>& sourcePaths,
                          const std::wstring& destination) {
    DWORD preferred = DROPEFFECT_NONE;
    if ((keyState & MK_CONTROL) != 0 && (keyState & MK_SHIFT) != 0) preferred = DROPEFFECT_LINK;
    else if ((keyState & MK_CONTROL) != 0) preferred = DROPEFFECT_COPY;
    else if ((keyState & MK_SHIFT) != 0) preferred = DROPEFFECT_MOVE;
    else if (!sourcePaths.empty() && !destination.empty()) {
        const std::wstring sourceRoot = std::filesystem::path(sourcePaths.front()).root_name().wstring();
        const std::wstring destinationRoot = std::filesystem::path(destination).root_name().wstring();
        preferred = _wcsicmp(sourceRoot.c_str(), destinationRoot.c_str()) == 0 ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
    }
    return (allowedEffects & preferred) != 0 ? preferred : DROPEFFECT_NONE;
}

HRESULT CreateFileDropTarget(DropDestinationProvider destination, FileDropHandler handler, IDropTarget** target) {
    if (target == nullptr) return E_POINTER;
    *target = new FileDropTarget(std::move(destination), std::move(handler));
    return S_OK;
}

} // namespace ffui
