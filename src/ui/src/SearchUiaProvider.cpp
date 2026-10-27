#include "SearchUiaProvider.h"
#include "UiaHostQuery.h"
#include "ffprotocol/Diagnostics.h"

#include "ffui/SearchResultMetadata.h" // indexed-search-allocated-size D9: shared Size-on-Disk wording
#include <algorithm>
#include <commctrl.h>   // ListView_* macros for the host list geometry
#include <cwctype>
#include <vector>
#include <wrl/implements.h> // Microsoft::WRL::Make

namespace ffui {

namespace {

constexpr HRESULT kElementNotAvailable = UIA_E_ELEMENTNOTAVAILABLE;

// original-vision-journey-recovery 4.9: opt-in rejection metadata distinguishes
// a retired row from an action-dispatch failure. No names, paths or content.
void TraceResultRejection(const wchar_t* reason, size_t index, uint64_t expected,
                          uint64_t actual, size_t count) noexcept {
    wchar_t enabled[8]{};
    if (GetEnvironmentVariableW(L"FASTFILES_UIA_DIAGNOSTICS", enabled, 8) != 1 || enabled[0] != L'1') return;
    try {
        ffprotocol::DiagnosticEvent event{};
        event.category = ffprotocol::DiagnosticCategory::UiInteraction;
        event.code = ffprotocol::DiagnosticCode::FFDIAG_UI_RESULT_SUPERSEDED;
        event.state = L"uia-result=" + std::wstring(reason) + L";index=" + std::to_wstring(index) +
            L";expected=" + std::to_wstring(expected) + L";actual=" + std::to_wstring(actual);
        event.itemCount = count;
        event.errorCode = static_cast<uint32_t>(UIA_E_ELEMENTNOTAVAILABLE);
        event.outcome = L"rejected";
        ffprotocol::AppendDiagnostic(event, ffprotocol::DiagnosticFormat::Json);
    } catch (...) { } // Observation must never change the rejection result.
}


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

// Logical-size formatter for a Search result's UIA FullDescription. It
// formats the candidate's logical `sizeBytes` only; allocated/on-disk size
// is rendered separately by `SearchAllocatedSizeText` so the two metrics
// stay visibly distinct (indexed-search-allocated-size D7/D9). The search
// model and engine pipeline now carry allocated size, but this helper
// remains the single source for the logical-size wording shared by the row
// renderer and the UIA provider.
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

// Reads a bounded custom runtime ID without casting. A real cross-process UIA
// client supplies a proxy object for startAfter, so this must never rely on
// the provider's concrete C++ layout. False when the element exposes no
// usable custom ID (HWND-backed roots) or fails resolution (stale elements).
bool ReadRuntimeId(IRawElementProviderSimple* element, std::vector<int>& values) {
    values.clear();
    if (element == nullptr) return false;
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> fragment;
    if (FAILED(element->QueryInterface(IID_IRawElementProviderFragment, &fragment))) return false;
    SAFEARRAY* ids = nullptr;
    if (FAILED(fragment->GetRuntimeId(&ids)) || ids == nullptr) return false;
    LONG lower = 0;
    LONG upper = -1;
    bool ok = SUCCEEDED(SafeArrayGetLBound(ids, 1, &lower)) &&
              SUCCEEDED(SafeArrayGetUBound(ids, 1, &upper));
    if (ok) {
        const LONGLONG count = static_cast<LONGLONG>(upper) - static_cast<LONGLONG>(lower) + 1;
        ok = count >= 2 && count <= 32;
        if (ok) {
            values.reserve(static_cast<size_t>(count));
            for (LONG i = lower; i <= upper; ++i) {
                int value = 0;
                if (FAILED(SafeArrayGetElement(ids, &i, &value))) {
                    values.clear();
                    ok = false;
                    break;
                }
                values.push_back(value);
            }
        }
    }
    SafeArrayDestroy(ids);
    return ok;
}

uint64_t RuntimeIdPair(const std::vector<int>& values, size_t lowIndex) {
    if (lowIndex + 1 >= values.size()) return 0;
    const uint64_t low = static_cast<uint32_t>(values[lowIndex]);
    const uint64_t high = static_cast<uint32_t>(values[lowIndex + 1]);
    return low | (high << 32);
}

size_t IndexOfResultProvider(SearchUiaProvider* owner, int column,
                             IRawElementProviderSimple* startAfter) {
    (void)column;
    if (startAfter == nullptr) return SIZE_MAX;
    // Resolve by the stable result identity encoded in the runtime ID. A
    // sibling from another surface, a stale result, or a proxy with malformed
    // data degrades to "start from the beginning"; none may be reinterpreted
    // as this provider's concrete C++ object.
    std::vector<int> values;
    if (!ReadRuntimeId(startAfter, values) || values.size() < 4 ||
        owner == nullptr || values[0] != owner->RuntimeNamespace() || values[1] != 1) {
        return SIZE_MAX;
    }
    const uint64_t idHash = RuntimeIdPair(values, 2);
    if (owner == nullptr || owner->Surface() == nullptr) return SIZE_MAX;
    const size_t count = owner->Surface()->UiaSearchResultCount();
    for (size_t index = 0; index < count; ++index) {
        const UiaSearchResultInfo info = owner->Surface()->UiaSearchResultInfoOf(index);
        if (SemanticListItemIdentity(info) == idHash) {
            return index;
        }
    }
    return SIZE_MAX;
}

} // namespace

// ---------------------------------------------------------------------------
// SearchUiaProvider
// ---------------------------------------------------------------------------

SearchUiaProvider::SearchUiaProvider(UiaOwnerStatePtr state, UiaSurface* surface, HWND listHwnd,
                                     SemanticListIdentity identity)
    : UiaProviderBase(std::move(state)), surface_(surface), listHwnd_(listHwnd), identity_(std::move(identity)) {
    // Lazy-init the HWND host in get_HostRawElementProvider. Construction
    // runs from WM_GETOBJECT; UiaHostProviderFromHwnd here re-enters the
    // subclass and overflows the stack.
}

SearchUiaProvider::~SearchUiaProvider() = default;

STDMETHODIMP SearchUiaProvider::get_HostRawElementProvider(IRawElementProviderSimple** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive()) return S_OK;
    if (UiaHostQueryGuard::Active(listHwnd_)) return S_OK;
    // Fragment root: expose the HWND host provider so UIA merges this custom
    // element with the list's default provider instead of replacing it.
    if (hostProvider_ == nullptr && listHwnd_ != nullptr && IsWindow(listHwnd_)) {
        Microsoft::WRL::ComPtr<IRawElementProviderSimple> host;
        UiaHostQueryGuard guard(listHwnd_);
        UiaHostProviderFromHwnd(listHwnd_, &host);
        if (host && host.Get() != static_cast<IRawElementProviderSimple*>(this)) {
            hostProvider_ = std::move(host);
        }
    }
    if (hostProvider_ != nullptr) {
        *retVal = hostProvider_.Get();
        (*retVal)->AddRef();
    }
    return S_OK;
}

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
            UiaProviderBase::SetVariantString(retVal, identity_.name);
            return S_OK;
        case UIA_AutomationIdPropertyId:
            UiaProviderBase::SetVariantString(retVal, identity_.automationId);
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
    if (hostProvider_ == nullptr && listHwnd_ != nullptr && IsWindow(listHwnd_)) {
        UiaHostQueryGuard guard(listHwnd_);
        UiaHostProviderFromHwnd(listHwnd_, &hostProvider_);
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
        const size_t headerCount = surface_->UiaListHeaderCount();
        if (headerCount > 0 && (count == 0 || direction == NavigateDirection_LastChild)) {
            auto header = HeaderElement(direction == NavigateDirection_LastChild ? headerCount - 1 : 0);
            if (header) *retVal = header.Detach();
            return S_OK;
        }
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
    if (hostProvider_ == nullptr && listHwnd_ != nullptr && IsWindow(listHwnd_)) {
        UiaHostQueryGuard guard(listHwnd_);
        UiaHostProviderFromHwnd(listHwnd_, &hostProvider_);
    }
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
    surface_->UiaListSetFocus();
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
    for (size_t index = 0; index < surface_->UiaListHeaderCount(); ++index) {
        RECT headerRect{};
        if (surface_->UiaListHeaderRect(index, headerRect) && x >= headerRect.left && x < headerRect.right &&
            y >= headerRect.top && y < headerRect.bottom) {
            auto header = HeaderElement(index);
            if (header) *retVal = header.Detach();
            return S_OK;
        }
    }
    // Native hit-testing accounts for report headers and the live scroll
    // offset; dividing by row height retargeted rows after vertical scroll.
    LVHITTESTINFO hit{};
    hit.pt = pt;
    const int row = ListView_HitTest(hwnd, &hit);
    if (row < 0 || static_cast<size_t>(row) >= surface_->UiaSearchResultCount()) return S_OK;
    auto result = ResultElement(static_cast<size_t>(row));
    if (result != nullptr) {
        *retVal = result.Detach();
    }
    return S_OK;
}

Microsoft::WRL::ComPtr<ListHeaderUiaProvider> SearchUiaProvider::HeaderElement(size_t index) {
    if (!Alive() || surface_ == nullptr || index >= surface_->UiaListHeaderCount()) return nullptr;
    return Microsoft::WRL::ComPtr<ListHeaderUiaProvider>(new ListHeaderUiaProvider(this, index));
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
    std::vector<Microsoft::WRL::ComPtr<SearchResultUiaProvider>> retained;
    const size_t count = surface_->UiaSearchResultCount();
    for (size_t index = 0; index < count; ++index) {
        if (surface_->UiaSearchResultInfoOf(index).selected) {
            const auto provider = ResultElement(index);
            if (provider != nullptr) {
                selected.push_back(provider.Get());
                retained.push_back(provider);
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
    const uint64_t idHash = SemanticListItemIdentity(info);
    const uint64_t key = UiaCombineKey(static_cast<uint64_t>(RuntimeNamespace()), idHash);
    std::lock_guard<std::recursive_mutex> lock(cacheMutex_);
    auto it = results_.find(key);
    if (it != results_.end() && it->second != nullptr && it->second->Index() == index) {
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
    std::lock_guard<std::recursive_mutex> lock(cacheMutex_);
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
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        TraceResultRejection(L"owner-unavailable", index_, idHash_, 0, 0);
        return false;
    }
    UiaSurface* surface = owner_->Surface();
    const size_t count = surface->UiaSearchResultCount();
    if (index_ >= count) {
        TraceResultRejection(L"outside-current-count", index_, idHash_, 0, count);
        return false;
    }
    info = surface->UiaSearchResultInfoOf(index_);
    const uint64_t actual = SemanticListItemIdentity(info);
    if (actual != idHash_) TraceResultRejection(L"identity-mismatch", index_, idHash_, actual, count);
    return actual == idHash_;
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
            UiaProviderBase::SetVariantString(retVal, info.displayFolder.empty() ? info.folder : info.displayFolder);
            return S_OK;
        case UIA_FullDescriptionPropertyId: {
            if (!info.description.empty()) {
                UiaProviderBase::SetVariantString(retVal, info.description);
                return S_OK;
            }
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
            UiaProviderBase::SetVariantBool(retVal, owner_->Surface()->UiaListResultOffscreen(index_, idHash_));
            return S_OK;
        case UIA_BoundingRectanglePropertyId: {
            RECT rect{};
            if (owner_->Surface()->UiaListResultRect(index_, idHash_, rect)) {
                SetVariantRect(retVal, rect);
            }
            return S_OK;
        }
        case UIA_RuntimeIdPropertyId:
            UiaProviderBase::SetVariantRuntimeId(retVal, owner_->ItemRuntimeId(idHash_));
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
            } else {
                auto header = owner_->HeaderElement(0);
                if (header) *retVal = header.Detach();
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
    *retVal = MakeIntArray(owner_->ItemRuntimeId(idHash_));
    return S_OK;
}

STDMETHODIMP SearchResultUiaProvider::get_BoundingRectangle(UiaRect* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    UiaSearchResultInfo info;
    if (!Resolve(info)) {
        return kElementNotAvailable;
    }
    RECT rect{};
    if (!owner_->Surface()->UiaListResultRect(index_, idHash_, rect)) {
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
    if (!owner_->Surface()->UiaListDispatchAction(index_, idHash_, 2)) return kElementNotAvailable;
    return S_OK;
}

STDMETHODIMP SearchResultUiaProvider::get_FragmentRoot(IRawElementProviderFragmentRoot** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive()) return kElementNotAvailable;
    return owner_->QueryInterface(IID_IRawElementProviderFragmentRoot, reinterpret_cast<void**>(retVal));
}

STDMETHODIMP SearchResultUiaProvider::Select() noexcept {
    UiaSearchResultInfo info;
    if (!Resolve(info)) {
        return kElementNotAvailable;
    }
    if (!owner_->Surface()->UiaListDispatchAction(index_, idHash_, 0)) {
        TraceResultRejection(L"select-dispatch", index_, idHash_, 0, 0);
        return kElementNotAvailable;
    }
    return S_OK;
}

STDMETHODIMP SearchResultUiaProvider::AddToSelection() noexcept {
    UiaSearchResultInfo info;
    if (!Resolve(info)) {
        return kElementNotAvailable;
    }
    if (!info.selected) {
        if (!owner_->Surface()->UiaListDispatchAction(index_, idHash_, 0)) return kElementNotAvailable;
    }
    return S_OK;
}

STDMETHODIMP SearchResultUiaProvider::RemoveFromSelection() noexcept {
    UiaSearchResultInfo info;
    if (!Resolve(info)) {
        return kElementNotAvailable;
    }
    if (info.selected) {
        if (!owner_->Surface()->UiaListDispatchAction(index_, idHash_, 1)) return kElementNotAvailable;
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
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive()) return kElementNotAvailable;
    return owner_->QueryInterface(IID_IRawElementProviderSimple, reinterpret_cast<void**>(retVal));
}

STDMETHODIMP SearchResultUiaProvider::Invoke() noexcept {
    UiaSearchResultInfo info;
    if (!Resolve(info)) {
        return kElementNotAvailable;
    }
    if (!owner_->Surface()->UiaListDispatchAction(index_, idHash_, 4)) return kElementNotAvailable;
    UiaProviderBase::RaiseInvoked(this);
    return S_OK;
}

STDMETHODIMP SearchResultUiaProvider::ScrollIntoView() noexcept {
    UiaSearchResultInfo info;
    if (!Resolve(info)) {
        return kElementNotAvailable;
    }
    if (!owner_->Surface()->UiaListDispatchAction(index_, idHash_, 3)) return kElementNotAvailable;
    return S_OK;
}

// Optional semantic report-list headers share the native list's sort path.
ListHeaderUiaProvider::ListHeaderUiaProvider(SearchUiaProvider* owner, size_t index)
    : UiaProviderBase(owner != nullptr ? owner->State() : nullptr), owner_(owner), index_(index) {
    if (owner_) {
        owner_->AddRef();
        identity_ = owner_->Surface()->UiaListHeaderIdentity(index_);
    }
}

ListHeaderUiaProvider::~ListHeaderUiaProvider() {
    if (owner_) owner_->Release();
}

bool ListHeaderUiaProvider::Available() const {
    return Alive() && owner_ != nullptr && owner_->SurfaceAlive() && owner_->Surface() != nullptr &&
           owner_->Surface()->UiaSearchVisible() && index_ < owner_->Surface()->UiaListHeaderCount() &&
           identity_ == owner_->Surface()->UiaListHeaderIdentity(index_);
}

STDMETHODIMP ListHeaderUiaProvider::GetPatternProvider(PATTERNID patternId, IUnknown** retVal) noexcept {
    if (!retVal) return E_POINTER;
    *retVal = nullptr;
    if (!Available()) return kElementNotAvailable;
    return patternId == UIA_InvokePatternId
        ? QueryInterface(IID_IInvokeProvider, reinterpret_cast<void**>(retVal)) : S_OK;
}

STDMETHODIMP ListHeaderUiaProvider::GetPropertyValue(PROPERTYID propertyId, VARIANT* retVal) noexcept {
    if (!retVal) return E_POINTER;
    VariantInit(retVal);
    if (!Available()) return kElementNotAvailable;
    switch (propertyId) {
    case UIA_ControlTypePropertyId: SetVariantInt(retVal, UIA_HeaderItemControlTypeId); break;
    case UIA_NamePropertyId: SetVariantString(retVal, owner_->Surface()->UiaListHeaderName(index_)); break;
    case UIA_IsControlElementPropertyId:
    case UIA_IsContentElementPropertyId:
    case UIA_IsEnabledPropertyId: SetVariantBool(retVal, true); break;
    case UIA_IsKeyboardFocusablePropertyId: SetVariantBool(retVal, false); break;
    case UIA_IsOffscreenPropertyId: {
        RECT rect{};
        SetVariantBool(retVal, !owner_->Surface()->UiaListHeaderRect(index_, rect) ||
                       rect.right <= rect.left || rect.bottom <= rect.top);
        break;
    }
    case UIA_BoundingRectanglePropertyId: {
        RECT rect{};
        if (owner_->Surface()->UiaListHeaderRect(index_, rect)) SetVariantRect(retVal, rect);
        break;
    }
    default: break;
    }
    return S_OK;
}

STDMETHODIMP ListHeaderUiaProvider::Navigate(NavigateDirection direction, IRawElementProviderFragment** retVal) noexcept {
    if (!retVal) return E_POINTER;
    *retVal = nullptr;
    if (!Available()) return kElementNotAvailable;
    if (direction == NavigateDirection_Parent) return owner_->QueryInterface(IID_PPV_ARGS(retVal));
    if (direction == NavigateDirection_NextSibling) {
        auto next = owner_->HeaderElement(index_ + 1);
        if (next) *retVal = next.Detach();
    } else if (direction == NavigateDirection_PreviousSibling) {
        if (index_ > 0) {
            auto previous = owner_->HeaderElement(index_ - 1);
            if (previous) *retVal = previous.Detach();
        } else {
            const auto count = owner_->Surface()->UiaSearchResultCount();
            if (count > 0) {
                auto previous = owner_->ResultElement(count - 1);
                if (previous) *retVal = previous.Detach();
            }
        }
    }
    return S_OK;
}

STDMETHODIMP ListHeaderUiaProvider::GetRuntimeId(SAFEARRAY** retVal) noexcept {
    if (!retVal) return E_POINTER;
    *retVal = nullptr;
    if (!Available()) return kElementNotAvailable;
    *retVal = MakeIntArray({owner_->RuntimeNamespace(), 2, static_cast<int>(index_),
                           UiaHashLow(identity_), UiaHashHigh(identity_)});
    return S_OK;
}

STDMETHODIMP ListHeaderUiaProvider::get_BoundingRectangle(UiaRect* retVal) noexcept {
    if (!retVal) return E_POINTER;
    if (!Available()) return kElementNotAvailable;
    RECT rect{};
    if (!owner_->Surface()->UiaListHeaderRect(index_, rect)) return kElementNotAvailable;
    *retVal = {static_cast<double>(rect.left), static_cast<double>(rect.top),
               static_cast<double>(rect.right - rect.left), static_cast<double>(rect.bottom - rect.top)};
    return S_OK;
}

STDMETHODIMP ListHeaderUiaProvider::GetEmbeddedFragmentRoots(SAFEARRAY** retVal) noexcept {
    if (!retVal) return E_POINTER;
    *retVal = nullptr;
    return S_OK;
}

STDMETHODIMP ListHeaderUiaProvider::SetFocus() noexcept { return Available() ? S_OK : kElementNotAvailable; }

STDMETHODIMP ListHeaderUiaProvider::get_FragmentRoot(IRawElementProviderFragmentRoot** retVal) noexcept {
    if (!retVal) return E_POINTER;
    *retVal = nullptr;
    if (!Available()) return kElementNotAvailable;
    return owner_->QueryInterface(IID_PPV_ARGS(retVal));
}

STDMETHODIMP ListHeaderUiaProvider::Invoke() noexcept {
    if (!Available()) return kElementNotAvailable;
    if (!owner_->Surface()->UiaListDispatchHeader(index_, identity_)) return kElementNotAvailable;
    return S_OK;
}
} // namespace ffui
