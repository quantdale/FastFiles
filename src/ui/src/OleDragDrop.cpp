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

// complete-ole-drag-source-move-effect D1/D2: the source advertises its
// preferred effect and consumes the effect the target reports it performed.
UINT PreferredDropEffectFormat() {
    return RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
}

UINT PerformedDropEffectFormat() {
    return RegisterClipboardFormatW(CFSTR_PERFORMEDDROPEFFECT);
}

HRESULT StoreDwordMedium(DWORD value, STGMEDIUM* medium) {
    HGLOBAL storage = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DWORD));
    if (storage == nullptr) return E_OUTOFMEMORY;
    auto* target = static_cast<DWORD*>(GlobalLock(storage));
    if (target == nullptr) {
        GlobalFree(storage);
        return E_OUTOFMEMORY;
    }
    *target = value;
    GlobalUnlock(storage);
    medium->tymed = TYMED_HGLOBAL;
    medium->hGlobal = storage;
    medium->pUnkForRelease = nullptr;
    return S_OK;
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
                if (length == 0) continue;
                std::wstring path(length + 1, L'\0');
                if (DragQueryFileW(drop, index, path.data(), length + 1) != 0) {
                    path.resize(length);
                    paths.push_back(std::move(path));
                }
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
    explicit FileDataObject(std::vector<std::wstring> paths, DWORD preferredEffect = DROPEFFECT_NONE)
        : paths_(std::move(paths)), preferredEffect_(preferredEffect) {}
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
        const UINT preferred = PreferredDropEffectFormat();
        if (preferred != 0 && format->cfFormat == preferred) {
            return StoreDwordMedium(preferredEffect_, medium);
        }
        const UINT performed = PerformedDropEffectFormat();
        if (performed != 0 && format->cfFormat == performed) {
            return StoreDwordMedium(performedDropEffect_, medium);
        }
        size_t characterCount = 1;
        for (const auto& path : paths_) {
            if (path.size() > SIZE_MAX / sizeof(wchar_t) - characterCount - 1) return E_OUTOFMEMORY;
            characterCount += path.size() + 1;
        }
        if (characterCount > (SIZE_MAX - sizeof(DROPFILES)) / sizeof(wchar_t)) return E_OUTOFMEMORY;
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
        if (format->dwAspect != DVASPECT_CONTENT || (format->tymed & TYMED_HGLOBAL) == 0) return DV_E_FORMATETC;
        if (format->cfFormat == CF_HDROP) return S_OK;
        const UINT preferred = PreferredDropEffectFormat();
        if (preferred != 0 && format->cfFormat == preferred && preferredEffect_ != DROPEFFECT_NONE) return S_OK;
        const UINT performed = PerformedDropEffectFormat();
        if (performed != 0 && format->cfFormat == performed && performedDropEffectWritten_) return S_OK;
        return DV_E_FORMATETC;
    }
    IFACEMETHODIMP GetCanonicalFormatEtc(FORMATETC*, FORMATETC* output) override {
        if (output != nullptr) output->ptd = nullptr;
        return E_NOTIMPL;
    }
    IFACEMETHODIMP SetData(FORMATETC* format, STGMEDIUM* medium, BOOL release) override {
        if (format == nullptr || medium == nullptr) return E_POINTER;
        // complete-ole-drag-source-move-effect D1: the drop target publishes
        // what it actually did (MOVE vs COPY) through CFSTR_PERFORMEDDROPEFFECT.
        const UINT performed = PerformedDropEffectFormat();
        if (performed == 0 || format->cfFormat != performed || format->dwAspect != DVASPECT_CONTENT ||
            (format->tymed & TYMED_HGLOBAL) == 0 || medium->tymed != TYMED_HGLOBAL || medium->hGlobal == nullptr) {
            // Unsupported: the caller keeps ownership of the medium.
            return E_NOTIMPL;
        }
        void* data = GlobalLock(medium->hGlobal);
        if (data == nullptr) return E_FAIL;
        performedDropEffect_ = *static_cast<const DWORD*>(data);
        performedDropEffectWritten_ = true;
        GlobalUnlock(medium->hGlobal);
        if (release) ReleaseStgMedium(medium);
        return S_OK;
    }
    IFACEMETHODIMP EnumFormatEtc(DWORD direction, IEnumFORMATETC** enumerator) override {
        if (enumerator == nullptr) return E_POINTER;
        *enumerator = nullptr;
        if (direction != DATADIR_GET) return E_NOTIMPL;
        FORMATETC formats[3]{};
        UINT count = 0;
        formats[count++] = DropFormat();
        const UINT preferred = PreferredDropEffectFormat();
        if (preferred != 0 && preferredEffect_ != DROPEFFECT_NONE) {
            formats[count++] = {static_cast<CLIPFORMAT>(preferred), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        }
        const UINT performed = PerformedDropEffectFormat();
        if (performed != 0 && performedDropEffectWritten_) {
            formats[count++] = {static_cast<CLIPFORMAT>(performed), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        }
        return SHCreateStdEnumFmtEtc(count, formats, enumerator);
    }
    IFACEMETHODIMP DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
    IFACEMETHODIMP DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    IFACEMETHODIMP EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }

private:
    std::atomic<ULONG> references_{1};
    std::vector<std::wstring> paths_;
    DWORD preferredEffect_ = DROPEFFECT_NONE;
    DWORD performedDropEffect_ = DROPEFFECT_NONE;
    bool performedDropEffectWritten_ = false;
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
    FileDropTarget(DropDestinationProvider destination, FileDropHandler handler, DropFeedbackSink feedback)
        : destination_(std::move(destination)), handler_(std::move(handler)), feedback_(std::move(feedback)) {}
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
    IFACEMETHODIMP DragEnter(IDataObject* object, DWORD keys, POINTL point, DWORD* effect) override {
        if (effect == nullptr) return E_POINTER;
        paths_ = ExtractPaths(object);
        *effect = DetermineDropEffect(keys, *effect, paths_, destination_(point));
        NotifyFeedback(true, point, *effect);
        return S_OK;
    }
    IFACEMETHODIMP DragOver(DWORD keys, POINTL point, DWORD* effect) override {
        if (effect == nullptr) return E_POINTER;
        *effect = DetermineDropEffect(keys, *effect, paths_, destination_(point));
        NotifyFeedback(true, point, *effect);
        return S_OK;
    }
    IFACEMETHODIMP DragLeave() override {
        paths_.clear();
        NotifyFeedback(false, POINTL{0, 0}, DROPEFFECT_NONE);
        return S_OK;
    }
    IFACEMETHODIMP Drop(IDataObject* object, DWORD keys, POINTL point, DWORD* effect) override {
        if (effect == nullptr) return E_POINTER;
        auto paths = ExtractPaths(object);
        // stabilize-live-uia-e2e: resolve the destination once, from the
        // final drop point, and hand it to the handler so the executed
        // operation matches the negotiated effect (the active pane at drop
        // time is the SOURCE pane -- the drag's mouse-down activated it).
        const std::wstring destination = destination_(point);
        const DWORD chosen = DetermineDropEffect(keys, *effect, paths, destination);
        *effect = chosen;
        NotifyFeedback(false, point, chosen);
        if (chosen != DROPEFFECT_NONE && !paths.empty()) handler_(std::move(paths), chosen, destination);
        paths_.clear();
        return S_OK;
    }
private:
    void NotifyFeedback(bool active, POINTL point, DWORD effect) {
        if (feedback_) {
            feedback_(active, point, effect);
        }
    }
    std::atomic<ULONG> references_{1};
    DropDestinationProvider destination_;
    FileDropHandler handler_;
    DropFeedbackSink feedback_;
    std::vector<std::wstring> paths_;
};

} // namespace

HRESULT CreateFileDataObject(const std::vector<std::wstring>& paths, IDataObject** object) {
    return CreateFileDataObject(paths, DROPEFFECT_NONE, object);
}

HRESULT CreateFileDataObject(const std::vector<std::wstring>& paths, DWORD preferredEffect, IDataObject** object) {
    if (object == nullptr) return E_POINTER;
    *object = nullptr;
    if (paths.empty()) return E_INVALIDARG;
    *object = new FileDataObject(paths, preferredEffect);
    return S_OK;
}

DWORD BeginFileDrag(const std::vector<std::wstring>& paths) {
    return BeginFileDrag(paths, 0);
}

DWORD BeginFileDrag(const std::vector<std::wstring>& paths, DWORD keyState) {
    // complete-ole-drag-source-move-effect D2: the modifier state is advertised
    // as the preferred effect (Ctrl=copy, Shift=move, Ctrl+Shift=link).
    const DWORD preferred = DetermineDropEffect(keyState, DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK,
                                                paths, std::wstring{});
    IDataObject* object = nullptr;
    if (FAILED(CreateFileDataObject(paths, preferred, &object))) return DROPEFFECT_NONE;
    auto* source = new FileDropSource();
    DWORD effect = DROPEFFECT_NONE;
    DoDragDrop(object, source, DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK, &effect);
    // The target's published performed effect is more authoritative than the
    // DoDragDrop return value; honor it when present (D1).
    const DWORD performed = PerformedDropEffectFromDataObject(object);
    if (performed != DROPEFFECT_NONE) effect = performed;
    source->Release();
    object->Release();
    return effect;
}

DWORD PerformedDropEffectFromDataObject(IDataObject* object) {
    if (object == nullptr) return DROPEFFECT_NONE;
    const UINT format = PerformedDropEffectFormat();
    if (format == 0) return DROPEFFECT_NONE;
    FORMATETC query{static_cast<CLIPFORMAT>(format), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium{};
    if (FAILED(object->GetData(&query, &medium))) return DROPEFFECT_NONE;
    DWORD effect = DROPEFFECT_NONE;
    if (medium.tymed == TYMED_HGLOBAL && medium.hGlobal != nullptr) {
        void* data = GlobalLock(medium.hGlobal);
        if (data != nullptr) {
            effect = *static_cast<const DWORD*>(data);
            GlobalUnlock(medium.hGlobal);
        }
    }
    ReleaseStgMedium(&medium);
    return effect;
}

std::vector<std::wstring> SourcePathsToDeleteAfterDrag(DWORD effect, const std::vector<std::wstring>& sourcePaths,
                                                       const std::function<bool(const std::wstring&)>& exists) {
    std::vector<std::wstring> result;
    if (effect != DROPEFFECT_MOVE) return result;
    for (const auto& path : sourcePaths) {
        if (exists(path)) result.push_back(path);
    }
    return result;
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
    return CreateFileDropTarget(std::move(destination), std::move(handler), DropFeedbackSink{}, target);
}

HRESULT CreateFileDropTarget(DropDestinationProvider destination, FileDropHandler handler,
                             DropFeedbackSink feedback, IDropTarget** target) {
    if (target == nullptr) return E_POINTER;
    *target = new FileDropTarget(std::move(destination), std::move(handler), std::move(feedback));
    return S_OK;
}

} // namespace ffui
