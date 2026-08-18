#include "SearchUiaProvider.h"

#include "ffui/SearchResultMetadata.h" // indexed-search-allocated-size D9: shared Size-on-Disk wording
#include <algorithm>
#include <commctrl.h>   // ListView_* macros for the host list geometry
#include <cwctype>
#include <vector>
#include <wrl/implements.h> // Microsoft::WRL::Make

namespace ffui {

namespace {

constexpr HRESULT kElementNotAvailable = UIA_E_ELEMENTNOTAVAILABLE;

SAFEARRAY* MakeIntArray(const std::vector<int>& values) {
    if (values.empty()) return nullptr;
    SAFEARRAYBOUND bound{static_cast<ULONG>(values.size()), 0};
    SAFEARRAY* array = SafeArrayCreate(VT_I4, 1, &bound);
    if (array == nullptr) return nullptr;
    for (LONG i = 0; i < static_cast<LONG>(values.size()); ++i) {
        (void)SafeArrayPutElement(array, &i, const_cast<int*>(&values[static_cast<size_t>(i)]));
    }
    return array;
}

SAFEARRAY* MakeRectArray(const RECT& rect) {
    const double values[4] = {static_cast<double>(rect.left), static_cast<double>(rect.top),
                              static_cast<double>(rect.right), static_cast<double>(rect.bottom)};
    SAFEARRAYBOUND bound{4, 0};
    SAFEARRAY* array = SafeArrayCreate(VT_R8, 1, &bound);
    if (array == nullptr) return nullptr;
    for (LONG i = 0; i < 4; ++i) {
        (void)SafeArrayPutElement(array, &i, const_cast<double*>(&values[i]));
    }
    return array;
}

SAFEARRAY* MakeUnknownArray(std::vector<IRawElementProviderSimple*> items) {
    SAFEARRAYBOUND bound{static_cast<ULONG>(items.size()), 0};
    SAFEARRAY* array = SafeArrayCreate(VT_UNKNOWN, 1, &bound);
    if (array == nullptr) return nullptr;
    for (LONG i = 0; i < static_cast<LONG>(items.size()); ++i) {
        if (items[static_cast<size_t>(i)] != nullptr) {
            items[static_cast<size_t>(i)]->AddRef();
            (void)SafeArrayPutElement(array, &i, items[static_cast<size_t>(i)]);
        }
    }
    return array;
}

void SetVariantRect(VARIANT* value, const RECT& rect) {
    VariantClear(value);
    value->vt = VT_R8 | VT_ARRAY;
    value->parray = MakeRectArray(rect);
}

bool NameMatches(const std::wstring& name, const VARIANT& value) {
    if (value.vt != VT_BSTR || value.bstrVal == nullptr) return false;
    const std::wstring expected(value.bstrVal, SysStringLen(value.bstrVal));
    return _wcsicmp(name.c_str(), expected.c_str()) == 0;
}

// Format a file's logical size (matches the details presentation; the
// search model has no allocated size and the pipeline is not widened).
std::wstring UiaSearchSizeText(uint64_t sizeBytes) {
    if (sizeBytes >= 1024ull * 1024ull * 1024ull) {
        return std::to_wstring(sizeBytes / (1024ull * 1024ull * 1024ull)) + L" GB";
    }
    if (sizeBytes >= 1024ull * 1024ull) {
        return std::to_wstring(sizeBytes / (1024ull * 1024ull)) + L" MB";
    }
    if (sizeBytes >= 1024ull) {
        return std::to_wstring(sizeBytes / 1024ull) + L" KB";
    }
    return std::to_wstring(sizeBytes) + L" bytes";
}

std::wstring UiaSearchTimeText(uint64_t modifiedTime) {
    if (modifiedTime == 0) return L"unknown";
    FILETIME fileTime{};
    fileTime.dwLowDateTime = static_cast<DWORD>(modifiedTime & 0xFFFFFFFFull);
    fileTime.dwHighDateTime = static_cast<DWORD>(modifiedTime >> 32);
    FILETIME local{};
    SYSTEMTIME system{};
    if (!FileTimeToLocalFileTime(&fileTime, &local) || !FileTimeToSystemTime(&local, &system)) {
        return L"unknown";
    }
    wchar_t buffer[64]{};
    swprintf_s(buffer, L"%04u-%02u-%02u %02u:%02u", system.wYear, system.wMonth,
               system.wDay, system.wHour, system.wMinute);
    return buffer;
}

size_t IndexOfResultProvider(SearchUiaProvider* owner, int column,
                             IRawElementProviderSimple* startAfter) {
    (void)owner;
    (void)column;
    if (startAfter == nullptr) return SIZE_MAX;
    Microsoft::WRL::ComPtr<ISelectionItemProvider> selection;
    if (FAILED(startAfter->QueryInterface(IID_ISelectionItemProvider, &selection))) return SIZE_MAX;
    auto* result = static_cast<SearchResultUiaProvider*>(selection.Get());
    return result->Index();
}

} // namespace

// ---------------------------------------------------------------------------
// SearchUiaProvider
// ---------------------------------------------------------------------------

SearchUiaProvider::SearchUiaProvider(UiaOwnerStatePtr state, UiaSurface* surface, HWND listHwnd)
    : UiaProviderBase(std::move(state)), surface_(surface), listHwnd_(listHwnd) {
    if (listHwnd_ != nullptr && IsWindow(listHwnd_)) {
        UiaHostProviderFromHwnd(listHwnd_, &hostProvider_);
    }
}

SearchUiaProvider::~SearchUiaProvider() = default;

STDMETHODIMP SearchUiaProvider::GetPatternProvider(PATTERNID patternId, IUnknown** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (patternId == UIA_SelectionPatternId) {
        return QueryInterface(IID_ISelectionProvider, reinterpret_cast<void**>(retVal));
    }
    if (patternId == UIA_ItemContainerPatternId) {
        return QueryInterface(IID_IItemContainerProvider, reinterpret_cast<void**>(retVal));
    }
    return S_OK;
}

STDMETHODIMP SearchUiaProvider::GetPropertyValue(PROPERTYID propertyId, VARIANT* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    VariantInit(retVal);
    if (!Alive() || surface_ == nullptr) return kElementNotAvailable;
    switch (propertyId) {
        case UIA_ControlTypePropertyId:
            UiaProviderBase::SetVariantInt(retVal, UIA_ListControlTypeId);
            return S_OK;
        case UIA_NamePropertyId:
            UiaProviderBase::SetVariantString(retVal, L"Search Results");
            return S_OK;
        case UIA_AutomationIdPropertyId:
            UiaProviderBase::SetVariantString(retVal, L"Search.Results");
            return S_OK;
        case UIA_IsKeyboardFocusablePropertyId:
            UiaProviderBase::SetVariantBool(retVal, true);
            return S_OK;
        case UIA_IsEnabledPropertyId:
            UiaProviderBase::SetVariantBool(retVal, true);
            return S_OK;
        case UIA_IsControlElementPropertyId:
        case UIA_IsContentElementPropertyId:
            UiaProviderBase::SetVariantBool(retVal, true);
            return S_OK;
        case UIA_ProviderDescriptionPropertyId:
            UiaProviderBase::SetVariantString(retVal, L"FastFiles Search Results provider");
            return S_OK;
        case UIA_RuntimeIdPropertyId:
            return S_OK; // HWND-backed root: runtime identity from the runtime
        default:
            break;
    }
    if (hostProvider_ != nullptr) {
        return hostProvider_->GetPropertyValue(propertyId, retVal);
    }
    return S_OK;
}

STDMETHODIMP SearchUiaProvider::Navigate(NavigateDirection direction, IRawElementProviderFragment** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || surface_ == nullptr) return kElementNotAvailable;
    if (direction == NavigateDirection_FirstChild || direction == NavigateDirection_LastChild) {
        const size_t count = surface_->UiaSearchResultCount();
        if (count == 0) return S_OK;
        const size_t index = direction == NavigateDirection_FirstChild ? 0 : count - 1;
        auto result = ResultElement(index);
        if (result != nullptr) {
            *retVal = result.Detach();
        }
    }
    return S_OK;
}

STDMETHODIMP SearchUiaProvider::GetRuntimeId(SAFEARRAY** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    return S_OK; // HWND-backed element
}

STDMETHODIMP SearchUiaProvider::get_BoundingRectangle(UiaRect* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    if (!Alive() || surface_ == nullptr) return kElementNotAvailable;
    if (hostProvider_ != nullptr) {
        Microsoft::WRL::ComPtr<IRawElementProviderFragment> fragment;
        if (SUCCEEDED(hostProvider_.As(&fragment))) {
            return fragment->get_BoundingRectangle(retVal);
        }
    }
    retVal->left = retVal->top = retVal->width = retVal->height = 0.0;
    return S_OK;
}

STDMETHODIMP SearchUiaProvider::GetEmbeddedFragmentRoots(SAFEARRAY** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    return S_OK;
}

STDMETHODIMP SearchUiaProvider::SetFocus() noexcept {
    if (!Alive() || surface_ == nullptr) return kElementNotAvailable;
    const HWND hwnd = surface_->UiaSearchHwnd();
    if (hwnd != nullptr && IsWindow(hwnd)) {
        ::SetFocus(hwnd);
    }
    return S_OK;
}

STDMETHODIMP SearchUiaProvider::get_FragmentRoot(IRawElementProviderFragmentRoot** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive()) return kElementNotAvailable;
    return QueryInterface(IID_IRawElementProviderFragmentRoot, reinterpret_cast<void**>(retVal));
}

STDMETHODIMP SearchUiaProvider::ElementProviderFromPoint(double x, double y,
                                                          IRawElementProviderFragment** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || surface_ == nullptr) return kElementNotAvailable;
    const HWND hwnd = surface_->UiaSearchHwnd();
    if (hwnd == nullptr || !IsWindow(hwnd)) return S_OK;
    POINT pt{static_cast<LONG>(x), static_cast<LONG>(y)};
    ScreenToClient(hwnd, &pt);
    RECT client{};
    GetClientRect(hwnd, &client);
    if (pt.x < client.left || pt.x >= client.right || pt.y < client.top || pt.y >= client.bottom) {
        return S_OK; // outside the list
    }
    const size_t count = surface_->UiaSearchResultCount();
    if (count == 0) return S_OK;
    // Row height from the native list's own spacing metric.
    const LONG spacing = ListView_GetItemSpacing(hwnd, TRUE);
    const LONG rowHeight = spacing > 0 ? LOWORD(static_cast<DWORD>(spacing)) : 28;
    if (rowHeight <= 0) return S_OK;
    const size_t row = static_cast<size_t>((pt.y - client.top) / rowHeight);
    if (row >= count) return S_OK;
    RECT itemRect{};
    if (!ListView_GetItemRect(hwnd, static_cast<int>(row), &itemRect, LVIR_BOUNDS)) return S_OK;
    if (pt.y < itemRect.top || pt.y >= itemRect.bottom) return S_OK;
    auto result = ResultElement(row);
    if (result != nullptr) {
        *retVal = result.Detach();
    }
    return S_OK;
}

STDMETHODIMP SearchUiaProvider::GetFocus(IRawElementProviderFragment** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || surface_ == nullptr) return kElementNotAvailable;
    if (!surface_->UiaSearchHasKeyboardFocus()) return S_OK;
    const size_t count = surface_->UiaSearchResultCount();
    for (size_t index = 0; index < count; ++index) {
        const UiaSearchResultInfo info = surface_->UiaSearchResultInfoOf(index);
        if (info.focused) {
            auto result = ResultElement(index);
            if (result != nullptr) {
                *retVal = result.Detach();
            }
            return S_OK;
        }
    }
    // The list itself owns focus when no row is focused.
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> self;
    if (SUCCEEDED(QueryInterface(IID_PPV_ARGS(&self)))) {
        *retVal = self.Detach();
    }
    return S_OK;
}

STDMETHODIMP SearchUiaProvider::GetSelection(SAFEARRAY** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || surface_ == nullptr) return kElementNotAvailable;
    std::vector<IRawElementProviderSimple*> selected;
    const size_t count = surface_->UiaSearchResultCount();
    for (size_t index = 0; index < count; ++index) {
        if (surface_->UiaSearchResultInfoOf(index).selected) {
            const auto provider = ResultElement(index);
            if (provider != nullptr) {
                selected.push_back(provider.Get());
            }
        }
    }
    *retVal = MakeUnknownArray(std::move(selected));
    return S_OK;
}

STDMETHODIMP SearchUiaProvider::get_CanSelectMultiple(BOOL* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = FALSE; // the list is LVS_SINGLESEL
    return S_OK;
}

STDMETHODIMP SearchUiaProvider::get_IsSelectionRequired(BOOL* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = FALSE;
    return S_OK;
}

STDMETHODIMP SearchUiaProvider::FindItemByProperty(IRawElementProviderSimple* startAfter,
                                                   PROPERTYID propertyId, VARIANT value,
                                                   IRawElementProviderSimple** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || surface_ == nullptr) return kElementNotAvailable;
    if (propertyId != UIA_NamePropertyId) {
        return UIA_E_INVALIDOPERATION;
    }
    size_t startIndex = IndexOfResultProvider(this, 0, startAfter);
    const size_t count = surface_->UiaSearchResultCount();
    for (size_t index = startIndex == SIZE_MAX ? 0 : startIndex + 1; index < count; ++index) {
        const UiaSearchResultInfo info = surface_->UiaSearchResultInfoOf(index);
        if (NameMatches(info.name, value)) {
            const auto provider = ResultElement(index);
            if (provider != nullptr) {
                return provider->QueryInterface(IID_IRawElementProviderSimple, reinterpret_cast<void**>(retVal));
            }
            return S_OK;
        }
    }
    return S_OK;
}

Microsoft::WRL::ComPtr<SearchResultUiaProvider> SearchUiaProvider::ResultElement(size_t index) {
    if (!Alive() || surface_ == nullptr) return nullptr;
    if (index >= surface_->UiaSearchResultCount()) return nullptr;
    const UiaSearchResultInfo info = surface_->UiaSearchResultInfoOf(index);
    const uint64_t idHash = UiaSearchIdentityHash(info.volumeId, info.id, info.folder, info.name);
    const uint64_t key = UiaCombineKey(static_cast<uint64_t>(kUiaNsSearch), idHash);
    auto it = results_.find(key);
    if (it != results_.end() && it->second != nullptr) {
        return it->second;
    }
    if (cacheSize_ >= kMaxCachedResults) {
        results_.clear();
        cacheSize_ = 0;
    }
    auto provider = Microsoft::WRL::ComPtr<SearchResultUiaProvider>(new SearchResultUiaProvider(this, index, idHash));
    ++cacheSize_;
    results_[key] = provider;
    return provider;
}

void SearchUiaProvider::InvalidateResults() {
    results_.clear();
    cacheSize_ = 0;
}

void SearchUiaProvider::SinkThunk(const UiaEventNotification& notification, void* context) {
    auto* self = static_cast<SearchUiaProvider*>(context);
    if (self != nullptr) {
        self->HandleNotification(notification);
    }
}

void SearchUiaProvider::HandleNotification(const UiaEventNotification& notification) {
    if (!Alive() || surface_ == nullptr) return;
    switch (notification.kind) {
        case UiaEventKind::StructureBulkRemoved:
        case UiaEventKind::StructureBulkAdded:
        case UiaEventKind::StructureChanged: {
            InvalidateResults();
            const StructureChangeType changeType =
                notification.kind == UiaEventKind::StructureBulkRemoved
                    ? StructureChangeType_ChildrenBulkRemoved
                    : (notification.kind == UiaEventKind::StructureBulkAdded
                           ? StructureChangeType_ChildrenBulkAdded
                           : StructureChangeType_ChildrenReordered);
            UiaProviderBase::RaiseStructureChanged(this, changeType, nullptr);
            return;
        }
        case UiaEventKind::SelectionInvalidated:
            UiaProviderBase::RaiseSelectionInvalidated(this);
            return;
        case UiaEventKind::ElementAddedToSelection:
        case UiaEventKind::ElementRemovedFromSelection: {
            const auto result = ResultElement(notification.searchIndex);
            if (result != nullptr) {
                if (notification.kind == UiaEventKind::ElementAddedToSelection) {
                    UiaProviderBase::RaiseElementAddedToSelection(result.Get());
                } else {
                    UiaProviderBase::RaiseElementRemovedFromSelection(result.Get());
                }
            }
            return;
        }
        case UiaEventKind::FocusChanged:
            NotifyFocusChanged();
            return;
        default:
            return;
    }
}

void SearchUiaProvider::NotifyFocusChanged() {
    if (!Alive() || surface_ == nullptr) return;
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> focused;
    if (SUCCEEDED(GetFocus(&focused)) && focused != nullptr) {
        Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple;
        if (SUCCEEDED(focused.As(&simple)) && simple != nullptr) {
            UiaProviderBase::RaiseFocusChanged(simple.Get());
        }
    }
}

// ---------------------------------------------------------------------------
// SearchResultUiaProvider
// ---------------------------------------------------------------------------

SearchResultUiaProvider::SearchResultUiaProvider(SearchUiaProvider* owner, size_t index, uint64_t idHash)
    : UiaProviderBase(owner != nullptr ? owner->State() : nullptr),
      owner_(owner), index_(index), idHash_(idHash) {}

bool SearchResultUiaProvider::Resolve(UiaSearchResultInfo& info) const {
    if (owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return false;
    }
    UiaSurface* surface = owner_->Surface();
    if (index_ >= surface->UiaSearchResultCount()) {
        return false;
    }
    info = surface->UiaSearchResultInfoOf(index_);
    return UiaSearchIdentityHash(info.volumeId, info.id, info.folder, info.name) == idHash_;
}

STDMETHODIMP SearchResultUiaProvider::GetPatternProvider(PATTERNID patternId, IUnknown** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (patternId == UIA_SelectionItemPatternId) {
        return QueryInterface(IID_ISelectionItemProvider, reinterpret_cast<void**>(retVal));
    }
    if (patternId == UIA_InvokePatternId) {
        return QueryInterface(IID_IInvokeProvider, reinterpret_cast<void**>(retVal));
    }
    if (patternId == UIA_ScrollItemPatternId) {
        return QueryInterface(IID_IScrollItemProvider, reinterpret_cast<void**>(retVal));
    }
    return S_OK;
}

STDMETHODIMP SearchResultUiaProvider::GetPropertyValue(PROPERTYID propertyId, VARIANT* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    VariantInit(retVal);
    UiaSearchResultInfo info;
    if (!Resolve(info)) {
        return kElementNotAvailable;
    }
    switch (propertyId) {
        case UIA_ControlTypePropertyId:
            UiaProviderBase::SetVariantInt(retVal, UIA_ListItemControlTypeId);
            return S_OK;
        case UIA_NamePropertyId:
            UiaProviderBase::SetVariantString(retVal, info.name);
            return S_OK;
        case UIA_HelpTextPropertyId:
            UiaProviderBase::SetVariantString(retVal, info.folder);
            return S_OK;
        case UIA_FullDescriptionPropertyId: {
            std::wstring description = info.isDirectory ? L"Folder" : L"File";
            if (!info.isDirectory) {
                description += L", " + UiaSearchSizeText(info.sizeBytes);
            }
            // indexed-search-allocated-size D9: expose Size on Disk through the
            // shared helper using the established UIA byte wording, so the
            // accessible description and the visible row agree on the semantics
            // (known bytes, known zero, or explicit unknown). No parallel
            // accessibility-only metadata source is introduced.
            description += L", " + SearchAllocatedSizeText(info.allocatedSizeBytes, info.allocatedSizeKnown,
                                                         UiaSearchSizeText);
            description += L", modified " + UiaSearchTimeText(info.modifiedTime);
            UiaProviderBase::SetVariantString(retVal, description);
            return S_OK;
        }
        case UIA_IsKeyboardFocusablePropertyId:
            UiaProviderBase::SetVariantBool(retVal, true);
            return S_OK;
        case UIA_HasKeyboardFocusPropertyId:
            UiaProviderBase::SetVariantBool(retVal, info.focused && owner_->Surface()->UiaSearchHasKeyboardFocus());
            return S_OK;
        case UIA_IsEnabledPropertyId:
            UiaProviderBase::SetVariantBool(retVal, true);
            return S_OK;
        case UIA_IsControlElementPropertyId:
        case UIA_IsContentElementPropertyId:
            UiaProviderBase::SetVariantBool(retVal, true);
            return S_OK;
        case UIA_IsOffscreenPropertyId:
            UiaProviderBase::SetVariantBool(retVal, owner_->Surface()->UiaSearchResultOffscreen(index_));
            return S_OK;
        case UIA_BoundingRectanglePropertyId: {
            RECT rect{};
            if (owner_->Surface()->UiaSearchResultRect(index_, rect)) {
                SetVariantRect(retVal, rect);
            }
            return S_OK;
        }
        case UIA_RuntimeIdPropertyId:
            UiaProviderBase::SetVariantRuntimeId(retVal, UiaSearchResultRuntimeId(idHash_));
            return S_OK;
        default:
            return S_OK;
    }
}

STDMETHODIMP SearchResultUiaProvider::Navigate(NavigateDirection direction, IRawElementProviderFragment** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    UiaSearchResultInfo info;
    if (!Resolve(info)) {
        return kElementNotAvailable;
    }
    switch (direction) {
        case NavigateDirection_Parent: {
            Microsoft::WRL::ComPtr<IRawElementProviderFragment> root;
            if (SUCCEEDED(owner_->QueryInterface(IID_PPV_ARGS(&root)))) {
                *retVal = root.Detach();
            }
            return S_OK;
        }
        case NavigateDirection_NextSibling: {
            auto next = owner_->ResultElement(index_ + 1);
            if (next != nullptr) {
                *retVal = next.Detach();
            }
            return S_OK;
        }
        case NavigateDirection_PreviousSibling: {
            if (index_ > 0) {
                auto previous = owner_->ResultElement(index_ - 1);
                if (previous != nullptr) {
                    *retVal = previous.Detach();
                }
            }
            return S_OK;
        }
        default:
            return S_OK;
    }
}

STDMETHODIMP SearchResultUiaProvider::GetRuntimeId(SAFEARRAY** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    UiaSearchResultInfo info;
    if (!Resolve(info)) {
        return kElementNotAvailable;
    }
    *retVal = MakeIntArray(UiaSearchResultRuntimeId(idHash_));
    return S_OK;
}

STDMETHODIMP SearchResultUiaProvider::get_BoundingRectangle(UiaRect* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    UiaSearchResultInfo info;
    if (!Resolve(info)) {
        return kElementNotAvailable;
    }
    RECT rect{};
    if (!owner_->Surface()->UiaSearchResultRect(index_, rect)) {
        return kElementNotAvailable;
    }
    retVal->left = static_cast<double>(rect.left);
    retVal->top = static_cast<double>(rect.top);
    retVal->width = static_cast<double>(rect.right - rect.left);
    retVal->height = static_cast<double>(rect.bottom - rect.top);
    return S_OK;
}

STDMETHODIMP SearchResultUiaProvider::GetEmbeddedFragmentRoots(SAFEARRAY** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    return S_OK;
}

STDMETHODIMP SearchResultUiaProvider::SetFocus() noexcept {
    UiaSearchResultInfo info;
    if (!Resolve(info)) {
        return kElementNotAvailable;
    }
    owner_->Surface()->UiaSearchSetFocusItem(index_);
    return S_OK;
}

STDMETHODIMP SearchResultUiaProvider::get_FragmentRoot(IRawElementProviderFragmentRoot** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (owner_ == nullptr || !owner_->SurfaceAlive()) return kElementNotAvailable;
    return owner_->QueryInterface(IID_IRawElementProviderFragmentRoot, reinterpret_cast<void**>(retVal));
}

STDMETHODIMP SearchResultUiaProvider::Select() noexcept {
    UiaSearchResultInfo info;
    if (!Resolve(info)) {
        return kElementNotAvailable;
    }
    owner_->Surface()->UiaSearchSelectItem(index_);
    return S_OK;
}

STDMETHODIMP SearchResultUiaProvider::AddToSelection() noexcept {
    UiaSearchResultInfo info;
    if (!Resolve(info)) {
        return kElementNotAvailable;
    }
    if (!info.selected) {
        owner_->Surface()->UiaSearchSelectItem(index_);
    }
    return S_OK;
}

STDMETHODIMP SearchResultUiaProvider::RemoveFromSelection() noexcept {
    UiaSearchResultInfo info;
    if (!Resolve(info)) {
        return kElementNotAvailable;
    }
    if (info.selected) {
        owner_->Surface()->UiaSearchRemoveFromSelection(index_);
    }
    return S_OK;
}

STDMETHODIMP SearchResultUiaProvider::get_IsSelected(BOOL* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = FALSE;
    UiaSearchResultInfo info;
    if (!Resolve(info)) {
        return kElementNotAvailable;
    }
    *retVal = info.selected ? TRUE : FALSE;
    return S_OK;
}

STDMETHODIMP SearchResultUiaProvider::get_SelectionContainer(IRawElementProviderSimple** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (owner_ == nullptr || !owner_->SurfaceAlive()) return kElementNotAvailable;
    return owner_->QueryInterface(IID_IRawElementProviderSimple, reinterpret_cast<void**>(retVal));
}

STDMETHODIMP SearchResultUiaProvider::Invoke() noexcept {
    UiaSearchResultInfo info;
    if (!Resolve(info)) {
        return kElementNotAvailable;
    }
    owner_->Surface()->UiaSearchActivateItem(index_);
    UiaProviderBase::RaiseInvoked(this);
    return S_OK;
}

STDMETHODIMP SearchResultUiaProvider::ScrollIntoView() noexcept {
    UiaSearchResultInfo info;
    if (!Resolve(info)) {
        return kElementNotAvailable;
    }
    owner_->Surface()->UiaSearchScrollIntoView(index_);
    return S_OK;
}

} // namespace ffui
