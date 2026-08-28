#include "ColumnViewUiaProvider.h"

#include "ffprotocol/Diagnostics.h"

#include <algorithm>
#include <cwctype>
#include <iterator>
#include <string>
#include <wrl/implements.h> // Microsoft::WRL::Make

namespace ffui {

namespace {

// Property/pattern constants used across the tree.
constexpr PROPERTYID kNameId = UIA_NamePropertyId;
constexpr PROPERTYID kControlTypeId = UIA_ControlTypePropertyId;
constexpr PROPERTYID kAutomationId = UIA_AutomationIdPropertyId;
constexpr PROPERTYID kIsKeyboardFocusableId = UIA_IsKeyboardFocusablePropertyId;
constexpr PROPERTYID kHasKeyboardFocusId = UIA_HasKeyboardFocusPropertyId;
constexpr PROPERTYID kIsEnabledId = UIA_IsEnabledPropertyId;
constexpr PROPERTYID kIsOffscreenId = UIA_IsOffscreenPropertyId;
constexpr PROPERTYID kBoundingRectangleId = UIA_BoundingRectanglePropertyId;
constexpr PROPERTYID kRuntimeIdId = UIA_RuntimeIdPropertyId;
constexpr PROPERTYID kIsControlElementId = UIA_IsControlElementPropertyId;
constexpr PROPERTYID kIsContentElementId = UIA_IsContentElementPropertyId;
constexpr PROPERTYID kHelpTextId = UIA_HelpTextPropertyId;
constexpr PROPERTYID kFullDescriptionId = UIA_FullDescriptionPropertyId;
constexpr PROPERTYID kProviderDescriptionId = UIA_ProviderDescriptionPropertyId;
constexpr PROPERTYID kIsPasswordId = UIA_IsPasswordPropertyId;
constexpr PROPERTYID kClassNameId = UIA_ClassNamePropertyId;
constexpr PROPERTYID kProcessIdId = UIA_ProcessIdPropertyId;
constexpr PROPERTYID kNativeWindowHandleId = UIA_NativeWindowHandlePropertyId;
constexpr PROPERTYID kItemStatusId = UIA_ItemStatusPropertyId;
constexpr PROPERTYID kIsKeyboardFocusableByUsId = UIA_IsKeyboardFocusablePropertyId; // (alias clarity)

// The error returned by a provider whose element no longer exists.
constexpr HRESULT kElementNotAvailable = UIA_E_ELEMENTNOTAVAILABLE;

bool UiInteractionDiagnosticsEnabled() {
    wchar_t value[8]{};
    const DWORD length = GetEnvironmentVariableW(L"FASTFILES_UIA_DIAGNOSTICS", value,
                                                  static_cast<DWORD>(std::size(value)));
    return length == 1 && value[0] == L'1';
}

void AppendProviderChildQueryDiagnostic(const wchar_t* operation, int paneCount) {
    if (!UiInteractionDiagnosticsEnabled()) return;
    ffprotocol::DiagnosticEvent event{};
    event.category = ffprotocol::DiagnosticCategory::UiInteraction;
    event.code = ffprotocol::DiagnosticCode::FFDIAG_UI_PROVIDER_CHILD_QUERY;
    event.state = L"operation=" + std::wstring(operation) + L";paneCount=" + std::to_wstring(paneCount);
    event.outcome = L"queried";
    ffprotocol::AppendDiagnostic(event, ffprotocol::DiagnosticFormat::Json);
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
    double values[4] = {static_cast<double>(rect.left), static_cast<double>(rect.top),
                        static_cast<double>(rect.right), static_cast<double>(rect.bottom)};
    SAFEARRAYBOUND bound{4, 0};
    SAFEARRAY* array = SafeArrayCreate(VT_R8, 1, &bound);
    if (array == nullptr) return nullptr;
    for (LONG i = 0; i < 4; ++i) {
        (void)SafeArrayPutElement(array, &i, &values[i]);
    }
    return array;
}

// The caller owns each pointer's reference: SafeArrayPutElement does not
// AddRef, and SafeArrayDestroy releases each element.
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

void SetVariantIntProperty(VARIANT* value, int32_t integer) {
    UiaProviderBase::SetVariantInt(value, integer);
}

// Case-insensitive ordinal name comparison (UIA Name matches are
// case-insensitive by convention).
bool NameMatches(const std::wstring& name, const VARIANT& value) {
    if (value.vt != VT_BSTR || value.bstrVal == nullptr) return false;
    const std::wstring expected(value.bstrVal, SysStringLen(value.bstrVal));
    return _wcsicmp(name.c_str(), expected.c_str()) == 0;
}

// Format a file's logical size for FullDescription (matches the details
// presentation; never fabricates an on-disk size).
std::wstring UiaSizeText(uint64_t sizeBytes) {
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

// Reads the leading (namespace, kind) discriminants of a provider's runtime
// ID without casting. False when the element exposes no usable custom ID
// (HWND-backed roots) or fails resolution (stale elements).
bool RuntimeIdKind(IRawElementProviderSimple* element, int* ns, int* kind) {
    *ns = -1;
    *kind = -1;
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> fragment;
    if (FAILED(element->QueryInterface(IID_IRawElementProviderFragment, &fragment))) return false;
    SAFEARRAY* ids = nullptr;
    if (FAILED(fragment->GetRuntimeId(&ids)) || ids == nullptr) return false;
    bool ok = false;
    LONG lower = 0;
    LONG upper = -1;
    if (SUCCEEDED(SafeArrayGetLBound(ids, 1, &lower)) &&
        SUCCEEDED(SafeArrayGetUBound(ids, 1, &upper)) && upper - lower + 1 >= 2) {
        LONG i0 = lower;
        LONG i1 = lower + 1;
        if (SUCCEEDED(SafeArrayGetElement(ids, &i0, ns)) &&
            SUCCEEDED(SafeArrayGetElement(ids, &i1, kind))) {
            ok = true;
        }
    }
    SafeArrayDestroy(ids);
    return ok;
}

// Resolve the item's row from a startAfter element for FindItemByProperty
// (unknown startAfter = start at the beginning). The cast is guarded by the
// runtime-ID discriminants: a sibling from a foreign surface is legal input
// and must degrade to "start from the beginning", never reinterpret fields.
int RowOfItemProvider(ColumnViewUiaProvider* owner, int pane, int column,
                      IRawElementProviderSimple* startAfter) {
    (void)owner;
    if (startAfter == nullptr) return -1;
    int ns = -1;
    int kind = -1;
    if (!RuntimeIdKind(startAfter, &ns, &kind) || ns != kUiaNsColumnView || kind != 3) return -1;
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> fragment;
    if (FAILED(startAfter->QueryInterface(IID_IRawElementProviderFragment, &fragment))) return -1;
    auto* item = static_cast<ColumnItemUiaProvider*>(fragment.Get());
    if (item->Pane() != pane || item->Column() != column) return -1;
    return item->Row();
}

} // namespace

// ---------------------------------------------------------------------------
// ColumnViewUiaProvider (fragment root / controller)
// ---------------------------------------------------------------------------

ColumnViewUiaProvider::ColumnViewUiaProvider(UiaOwnerStatePtr state, UiaSurface* surface, HWND hwnd,
                                             TreemapUiaSurface* treemapSurface)
    : UiaProviderBase(std::move(state)), surface_(surface),
      fragmentState_(std::make_shared<UiaOwnerState>()), treemapSurface_(treemapSurface), hwnd_(hwnd) {
    if (hwnd_ != nullptr && IsWindow(hwnd_)) {
        UiaHostProviderFromHwnd(hwnd_, &hostProvider_);
    }
    // semantic-treemap-uia 30.x: subscribe to the treemap semantic events.
    // The controller raises standard UIA events for structure/property
    // changes and invalidates the provider cache (see HandleNotification).
    if (treemapSurface_ != nullptr && treemapSurface_->TreemapEvents() != nullptr) {
        treemapSurface_->TreemapEvents()->Set(&ColumnViewUiaProvider::SinkThunk, this);
        treemapEventsSubscribed_ = true;
    }
}

void ColumnViewUiaProvider::SetTreemapSurface(TreemapUiaSurface* treemapSurface) {
    // Re-wire the event subscription when the surface is attached after the
    // provider was created (the shell wires it once at startup).
    if (treemapEventsSubscribed_) {
        if (treemapSurface_ != nullptr && treemapSurface_->TreemapEvents() != nullptr) {
            treemapSurface_->TreemapEvents()->Clear();
        }
        treemapEventsSubscribed_ = false;
    }
    treemapSurface_ = treemapSurface;
    if (treemapSurface_ != nullptr && treemapSurface_->TreemapEvents() != nullptr) {
        treemapSurface_->TreemapEvents()->Set(&ColumnViewUiaProvider::SinkThunk, this);
        treemapEventsSubscribed_ = true;
    }
    InvalidateTreemap();
}

ColumnViewUiaProvider::~ColumnViewUiaProvider() {
    // The controller owns the subscription; detach it so the surface never
    // calls back into a freed provider. Safe on any thread: the surface's
    // router clears plain scalar fields and the surface outlives the tree
    // (ColumnView::ReleaseUiaProvider runs on the UI thread before the
    // surface is torn down).
    if (treemapEventsSubscribed_ && treemapSurface_ != nullptr &&
        treemapSurface_->TreemapEvents() != nullptr) {
        treemapSurface_->TreemapEvents()->Clear();
        treemapEventsSubscribed_ = false;
    }
}

STDMETHODIMP ColumnViewUiaProvider::GetPatternProvider(PATTERNID /*patternId*/, IUnknown** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    return S_OK;
}

STDMETHODIMP ColumnViewUiaProvider::GetPropertyValue(PROPERTYID propertyId, VARIANT* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    VariantInit(retVal);
    if (!Alive() || surface_ == nullptr) return kElementNotAvailable;
    switch (propertyId) {
        case UIA_ControlTypePropertyId:
            SetVariantIntProperty(retVal, UIA_WindowControlTypeId);
            return S_OK;
        case UIA_AutomationIdPropertyId:
            SetVariantString(retVal, L"MainWindow");
            return S_OK;
        case UIA_IsKeyboardFocusablePropertyId:
            SetVariantBool(retVal, true);
            return S_OK;
        case UIA_IsEnabledPropertyId:
            SetVariantBool(retVal, true);
            return S_OK;
        case UIA_IsControlElementPropertyId:
        case UIA_IsContentElementPropertyId:
            SetVariantBool(retVal, true);
            return S_OK;
        case UIA_ProviderDescriptionPropertyId:
            SetVariantString(retVal, L"FastFiles Column View provider");
            return S_OK;
        case UIA_RuntimeIdPropertyId:
            // HWND-backed roots get their runtime identity from the runtime.
            return S_OK;
        default:
            break;
    }
    // Defer window-level properties (Name, bounds, class, process, ...) to
    // the host provider so they stay live with the window.
    if (hostProvider_ != nullptr) {
        return hostProvider_->GetPropertyValue(propertyId, retVal);
    }
    return S_OK;
}

STDMETHODIMP ColumnViewUiaProvider::get_HostRawElementProvider(IRawElementProviderSimple** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive()) return S_OK;
    // Fragment root: expose the HWND host provider so UIA merges this custom
    // element with the window's default provider instead of replacing it
    // (keeps native child controls and window properties reachable).
    if (hostProvider_ != nullptr) {
        *retVal = hostProvider_.Get();
        (*retVal)->AddRef();
    }
    return S_OK;
}

STDMETHODIMP ColumnViewUiaProvider::Navigate(NavigateDirection direction, IRawElementProviderFragment** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || surface_ == nullptr) return kElementNotAvailable;
    if (direction == NavigateDirection_FirstChild || direction == NavigateDirection_LastChild) {
        // semantic-treemap-uia 10.x: the provider tree describes what is
        // ACTUALLY visible -- the treemap group replaces the Column View
        // group while the storage Treemap view is active, instead of
        // exposing an obscured Column View subtree.
        if (treemapSurface_ != nullptr && treemapSurface_->TreemapActive()) {
            auto treemapGroup = TreemapGroupElement();
            if (treemapGroup != nullptr) {
                *retVal = treemapGroup.Detach();
            }
            return S_OK;
        }
        auto group = GroupElement();
        if (group != nullptr) {
            *retVal = group.Detach();
        }
        return S_OK;
    }
    return S_OK; // root has no parent or siblings
}

STDMETHODIMP ColumnViewUiaProvider::GetRuntimeId(SAFEARRAY** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    return S_OK; // HWND-backed elements use the runtime's identity
}

STDMETHODIMP ColumnViewUiaProvider::get_BoundingRectangle(UiaRect* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    if (!Alive() || surface_ == nullptr) return kElementNotAvailable;
    if (hostProvider_ != nullptr) {
        Microsoft::WRL::ComPtr<IRawElementProviderFragment> fragment;
        if (SUCCEEDED(hostProvider_.As(&fragment))) {
            return fragment->get_BoundingRectangle(retVal);
        }
    }
    RECT rect{};
    if (hwnd_ != nullptr && GetWindowRect(hwnd_, &rect)) {
        retVal->left = static_cast<double>(rect.left);
        retVal->top = static_cast<double>(rect.top);
        retVal->width = static_cast<double>(rect.right - rect.left);
        retVal->height = static_cast<double>(rect.bottom - rect.top);
        return S_OK;
    }
    retVal->left = retVal->top = retVal->width = retVal->height = 0.0;
    return S_OK;
}

STDMETHODIMP ColumnViewUiaProvider::GetEmbeddedFragmentRoots(SAFEARRAY** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    return S_OK;
}

STDMETHODIMP ColumnViewUiaProvider::SetFocus() noexcept {
    if (!Alive() || surface_ == nullptr) return kElementNotAvailable;
    if (hwnd_ != nullptr && IsWindow(hwnd_)) {
        ::SetFocus(hwnd_);
    }
    return S_OK;
}

STDMETHODIMP ColumnViewUiaProvider::get_FragmentRoot(IRawElementProviderFragmentRoot** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive()) return kElementNotAvailable;
    return QueryInterface(IID_IRawElementProviderFragmentRoot, reinterpret_cast<void**>(retVal));
}

STDMETHODIMP ColumnViewUiaProvider::ElementProviderFromPoint(double x, double y,
                                                              IRawElementProviderFragment** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || surface_ == nullptr) return kElementNotAvailable;

    const UiaViewGeometry geometry = Geometry();
    POINT pt{static_cast<LONG>(x), static_cast<LONG>(y)};
    if (geometry.hwnd != nullptr && IsWindow(geometry.hwnd)) {
        ScreenToClient(geometry.hwnd, &pt);
    }
    const double scale = geometry.dpiScale > 0.0 ? geometry.dpiScale : 1.0;
    const double xDip = static_cast<double>(pt.x) / scale - geometry.originXDip;
    const double yDip = static_cast<double>(pt.y) / scale - geometry.originYDip;
    if (xDip < 0.0 || xDip >= geometry.widthDip || yDip < 0.0 || yDip >= geometry.heightDip) {
        return S_OK; // outside the navigation viewport
    }

    // semantic-treemap-uia 18.x: while the treemap is active, hit-testing
    // goes through the EXACT cached treemap layout (same HitTest the mouse
    // click/hover use), converted to normalized 0..100 space with the same
    // viewport divide. The result MUST agree with HandleLButtonDown.
    if (treemapSurface_ != nullptr && treemapSurface_->TreemapActive()) {
        const float normalizedX = geometry.widthDip > 0.0f
                                      ? static_cast<float>(xDip / geometry.widthDip) * 100.0f
                                      : -1.0f;
        const float normalizedY = geometry.heightDip > 0.0f
                                      ? static_cast<float>(yDip / geometry.heightDip) * 100.0f
                                      : -1.0f;
        const TreemapUiaItemInfo tile = treemapSurface_->TreemapTileHitTest(normalizedX, normalizedY);
        if (tile.identityHash != 0) {
            auto tileElement = TreemapTileElement(tile.scopeHash, tile.identityHash);
            if (tileElement != nullptr) {
                *retVal = tileElement.Detach();
                return S_OK;
            }
        }
        // Gutter/empty area inside the treemap viewport resolves to the group.
        auto treemapGroup = TreemapGroupElement();
        if (treemapGroup != nullptr) {
            *retVal = treemapGroup.Detach();
        }
        return S_OK;
    }

    const int paneCount = surface_->UiaPaneCount();
    const float paneWidth = geometry.widthDip / static_cast<float>(paneCount);
    const int pane = paneCount > 1 ? (xDip < paneWidth ? 0 : 1) : 0;
    if (pane < 0 || pane >= paneCount) return S_OK;

    auto paneElement = PaneElement(pane);
    if (paneElement == nullptr) return S_OK;

    if (surface_->UiaPaneIsHome(pane)) {
        const float vScroll = surface_->UiaVerticalScroll(pane);
        const int sectionCount = surface_->UiaHomeSectionCount(pane);
        for (int ordinal = 0; ordinal < sectionCount; ++ordinal) {
            const UiaHomeSectionInfo section = surface_->UiaHomeSectionInfoOf(pane, ordinal);
            if (section.clearAffordance) {
                const D2D1_RECT_F clearRect = UiaHomeHeaderRectDip(
                    static_cast<float>(pane) * paneWidth, section.headerX + section.headerWidth - 44.0f,
                    section.headerY, 44.0f, section.headerHeight, vScroll);
                if (xDip >= clearRect.left && xDip < clearRect.right &&
                    yDip >= clearRect.top && yDip < clearRect.bottom) {
                    const int clearKind = section.sectionKind == kUiaHomeSectionRecentFolders
                        ? kUiaHomeClearRecentFolders : kUiaHomeClearRecentSearches;
                    auto clear = HomeClearElement(pane, clearKind);
                    if (clear != nullptr) {
                        *retVal = clear.Detach();
                        return S_OK;
                    }
                }
            }
            const D2D1_RECT_F headerRect = UiaHomeHeaderRectDip(
                static_cast<float>(pane) * paneWidth, section.headerX, section.headerY,
                section.headerWidth, section.headerHeight, vScroll);
            if (xDip >= headerRect.left && xDip < headerRect.right &&
                yDip >= headerRect.top && yDip < headerRect.bottom) {
                auto sectionElement = HomeSectionElement(pane, ordinal);
                if (sectionElement != nullptr) {
                    *retVal = sectionElement.Detach();
                    return S_OK;
                }
            }
        }
        const int itemCount = surface_->UiaHomeItemCount(pane);
        for (int globalIndex = 0; globalIndex < itemCount; ++globalIndex) {
            const UiaHomeItemInfo item = surface_->UiaHomeItemInfoOf(pane, globalIndex);
            const D2D1_RECT_F itemRect = UiaHomeItemRectDip(
                static_cast<float>(pane) * paneWidth, item.x, item.y, item.width, item.height, vScroll);
            if (xDip >= itemRect.left && xDip < itemRect.right &&
                yDip >= itemRect.top && yDip < itemRect.bottom) {
                auto itemElement = HomeItemElement(pane, globalIndex);
                if (itemElement != nullptr) {
                    *retVal = itemElement.Detach();
                    return S_OK;
                }
            }
        }
        *retVal = paneElement.Detach();
        return S_OK;
    }

    const float hScroll = UiaPaneScrollDip(geometry, pane);
    const int columnCount = surface_->UiaColumnCount(pane);
    for (int column = 0; column < columnCount; ++column) {
        const D2D1_RECT_F columnRect = UiaColumnRectDip(
            column, static_cast<float>(pane) * paneWidth, paneWidth, hScroll, geometry.heightDip);
        if (xDip < columnRect.left || xDip >= columnRect.right) continue;
        const UiaColumnInfo info = surface_->UiaColumnInfoOf(pane, column);
        if (info.itemCount > 0 && info.status != UiaColumnStatus::Unavailable) {
            const bool statusLine = info.status == UiaColumnStatus::Loading;
            const float vScroll = surface_->UiaVerticalScroll(pane);
            const float contentTop = UiaContentTopDip(statusLine);
            const int row = static_cast<int>((yDip - contentTop + vScroll) / UiaBrowseRowHeightDip());
            if (row >= 0 && row < info.itemCount) {
                const D2D1_RECT_F itemRect = UiaItemRectDip(
                    column, row, static_cast<float>(pane) * paneWidth, paneWidth, hScroll, vScroll,
                    statusLine, geometry.heightDip);
                if (yDip >= itemRect.top && yDip < itemRect.bottom) {
                    auto item = ItemElement(pane, column, row);
                    if (item != nullptr) {
                        *retVal = item.Detach();
                        return S_OK;
                    }
                }
            }
        }
        auto columnElement = ColumnElement(pane, column);
        if (columnElement != nullptr) {
            *retVal = columnElement.Detach();
            return S_OK;
        }
    }
    *retVal = paneElement.Detach();
    return S_OK;
}

STDMETHODIMP ColumnViewUiaProvider::GetFocus(IRawElementProviderFragment** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || surface_ == nullptr) return kElementNotAvailable;
    // semantic-treemap-uia 9.x: the treemap has no keyboard-focus model.
    // While it is active there is no real focus target in the navigation
    // surface, so GetFocus honestly reports null instead of pretending the
    // obscured Column View is focused.
    if (treemapSurface_ != nullptr && treemapSurface_->TreemapActive()) {
        return S_OK;
    }
    auto focused = FocusElement();
    if (focused != nullptr) {
        Microsoft::WRL::ComPtr<IRawElementProviderFragment> fragment;
        if (SUCCEEDED(focused.As(&fragment))) {
            *retVal = fragment.Detach();
        }
    }
    return S_OK;
}

Microsoft::WRL::ComPtr<UiaProviderBase> ColumnViewUiaProvider::FocusElement() {
    if (!Alive() || surface_ == nullptr) return nullptr;
    const int active = surface_->UiaActivePane();
    if (surface_->UiaPaneIsHome(active)) {
        const int target = surface_->UiaHomeFocusTarget(active);
        if (target == kUiaHomeFocusItem) {
            return HomeItemElement(active, surface_->UiaHomeFocusIndex(active));
        }
        if (target == kUiaHomeFocusClearRecentFolders) {
            return HomeClearElement(active, kUiaHomeClearRecentFolders);
        }
        if (target == kUiaHomeFocusClearRecentSearches) {
            return HomeClearElement(active, kUiaHomeClearRecentSearches);
        }
        return nullptr;
    }
    const int column = surface_->UiaFocusedColumn(active);
    if (column < 0 || column >= surface_->UiaColumnCount(active)) return nullptr;
    const int row = surface_->UiaFocusedRow(active, column);
    if (row >= 0) {
        return ItemElement(active, column, row);
    }
    return ColumnElement(active, column);
}

int ColumnViewUiaProvider::PaneAtPoint(double xDip, double yDip) const {
    const UiaViewGeometry geometry = Geometry();
    if (xDip < 0.0 || xDip >= geometry.widthDip || yDip < 0.0 || yDip >= geometry.heightDip) {
        return -1;
    }
    const int paneCount = surface_ != nullptr ? surface_->UiaPaneCount() : 0;
    if (paneCount <= 0) return -1;
    const float paneWidth = geometry.widthDip / static_cast<float>(paneCount);
    const int pane = paneCount > 1 ? (xDip < paneWidth ? 0 : 1) : 0;
    return pane < paneCount ? pane : -1;
}

bool ColumnViewUiaProvider::ResolveColumnIdentity(int pane, int column, uint64_t& pathHash) const {
    if (surface_ == nullptr) return false;
    if (column < 0 || column >= surface_->UiaColumnCount(pane)) return false;
    const UiaColumnInfo info = surface_->UiaColumnInfoOf(pane, column);
    pathHash = UiaHashWide(info.path);
    return true;
}

// --- factories -------------------------------------------------------------

Microsoft::WRL::ComPtr<ColumnViewGroupUiaProvider> ColumnViewUiaProvider::GroupElement() {
    if (!Alive() || surface_ == nullptr) return nullptr;
    if (group_ == nullptr) {
        group_ = Microsoft::WRL::ComPtr<ColumnViewGroupUiaProvider>(new ColumnViewGroupUiaProvider(this));
    }
    return group_;
}

Microsoft::WRL::ComPtr<PaneUiaProvider> ColumnViewUiaProvider::PaneElement(int pane) {
    if (!Alive() || surface_ == nullptr || pane < 0 || pane > 1) return nullptr;
    if (panes_[static_cast<size_t>(pane)] == nullptr) {
        panes_[static_cast<size_t>(pane)] = Microsoft::WRL::ComPtr<PaneUiaProvider>(new PaneUiaProvider(this, pane));
    }
    return panes_[static_cast<size_t>(pane)];
}

Microsoft::WRL::ComPtr<ColumnUiaProvider> ColumnViewUiaProvider::ColumnElement(int pane, int column) {
    if (!Alive() || surface_ == nullptr) return nullptr;
    uint64_t pathHash = 0;
    if (!ResolveColumnIdentity(pane, column, pathHash)) return nullptr;
    const uint64_t key = UiaCombineKey(static_cast<uint64_t>(pane), pathHash);
    auto it = columns_.find(key);
    if (it != columns_.end() && it->second != nullptr) {
        return it->second;
    }
    if (columns_.size() >= kMaxCachedColumns) {
        columns_.clear();
    }
    auto provider = Microsoft::WRL::ComPtr<ColumnUiaProvider>(new ColumnUiaProvider(this, pane, column, pathHash));
    columns_[key] = provider;
    return provider;
}

Microsoft::WRL::ComPtr<ColumnItemUiaProvider> ColumnViewUiaProvider::ItemElement(int pane, int column, int row) {
    if (!Alive() || surface_ == nullptr) return nullptr;
    uint64_t pathHash = 0;
    if (!ResolveColumnIdentity(pane, column, pathHash)) return nullptr;
    const UiaColumnInfo columnInfo = surface_->UiaColumnInfoOf(pane, column);
    if (row < 0 || row >= columnInfo.itemCount) return nullptr;
    const UiaItemInfo item = surface_->UiaItemInfoOf(pane, column, row);
    const uint64_t idHash = UiaItemIdentityHash(item.volumeRowId, item.fileIdLow, item.fileIdHigh, item.fullPath);
    const uint64_t key = UiaCombineKey(static_cast<uint64_t>(pane), pathHash, idHash);
    auto it = items_.find(key);
    if (it != items_.end() && it->second != nullptr) {
        return it->second;
    }
    if (itemCacheSize_ >= kMaxCachedItems) {
        items_.clear();
        itemCacheSize_ = 0;
    }
    auto provider = Microsoft::WRL::ComPtr<ColumnItemUiaProvider>(new ColumnItemUiaProvider(this, pane, column, row, pathHash, idHash));
    ++itemCacheSize_;
    items_[key] = provider;
    return provider;
}

Microsoft::WRL::ComPtr<ColumnStatusUiaProvider> ColumnViewUiaProvider::StatusElement(int pane, int column) {
    if (!Alive() || surface_ == nullptr) return nullptr;
    uint64_t pathHash = 0;
    if (!ResolveColumnIdentity(pane, column, pathHash)) return nullptr;
    const uint64_t key = UiaCombineKey(static_cast<uint64_t>(pane), pathHash);
    auto it = statuses_.find(key);
    if (it != statuses_.end() && it->second != nullptr) {
        return it->second;
    }
    auto provider = Microsoft::WRL::ComPtr<ColumnStatusUiaProvider>(new ColumnStatusUiaProvider(this, pane, column));
    statuses_[key] = provider;
    return provider;
}

Microsoft::WRL::ComPtr<HomeGroupUiaProvider> ColumnViewUiaProvider::HomeGroupElement(int pane) {
    if (!Alive() || surface_ == nullptr || pane < 0 || pane > 1) return nullptr;
    if (homeGroups_[static_cast<size_t>(pane)] == nullptr) {
        homeGroups_[static_cast<size_t>(pane)] = Microsoft::WRL::ComPtr<HomeGroupUiaProvider>(new HomeGroupUiaProvider(this, pane));
    }
    return homeGroups_[static_cast<size_t>(pane)];
}

Microsoft::WRL::ComPtr<HomeSectionUiaProvider> ColumnViewUiaProvider::HomeSectionElement(int pane, int ordinal) {
    if (!Alive() || surface_ == nullptr) return nullptr;
    if (ordinal < 0 || ordinal >= surface_->UiaHomeSectionCount(pane)) return nullptr;
    const uint64_t key = UiaCombineKey(static_cast<uint64_t>(pane), static_cast<uint64_t>(ordinal));
    auto it = homeSections_.find(key);
    if (it != homeSections_.end() && it->second != nullptr) {
        return it->second;
    }
    auto provider = Microsoft::WRL::ComPtr<HomeSectionUiaProvider>(new HomeSectionUiaProvider(this, pane, ordinal));
    homeSections_[key] = provider;
    return provider;
}

Microsoft::WRL::ComPtr<HomeItemUiaProvider> ColumnViewUiaProvider::HomeItemElement(int pane, int globalIndex) {
    if (!Alive() || surface_ == nullptr) return nullptr;
    if (globalIndex < 0 || globalIndex >= surface_->UiaHomeItemCount(pane)) return nullptr;
    const UiaHomeItemInfo item = surface_->UiaHomeItemInfoOf(pane, globalIndex);
    const uint64_t valueHash = UiaHashWide(item.value);
    const uint64_t key = UiaCombineKey(static_cast<uint64_t>(pane),
                                       UiaCombineKey(static_cast<uint64_t>(item.sectionKind),
                                                     static_cast<uint64_t>(item.indexInSection)),
                                       valueHash);
    auto it = homeItems_.find(key);
    if (it != homeItems_.end() && it->second != nullptr) {
        return it->second;
    }
    if (homeItemCacheSize_ >= kMaxCachedHomeItems) {
        homeItems_.clear();
        homeItemCacheSize_ = 0;
    }
    auto provider = Microsoft::WRL::ComPtr<HomeItemUiaProvider>(
        new HomeItemUiaProvider(this, pane, globalIndex, item.sectionKind, item.indexInSection, valueHash));
    ++homeItemCacheSize_;
    homeItems_[key] = provider;
    return provider;
}

Microsoft::WRL::ComPtr<HomeClearUiaProvider> ColumnViewUiaProvider::HomeClearElement(int pane, int clearKind) {
    if (!Alive() || surface_ == nullptr) return nullptr;
    const int sectionKind = clearKind == kUiaHomeClearRecentFolders
        ? kUiaHomeSectionRecentFolders : kUiaHomeSectionRecentSearches;
    const uint64_t key = UiaCombineKey(static_cast<uint64_t>(pane), static_cast<uint64_t>(sectionKind));
    auto it = homeClears_.find(key);
    if (it != homeClears_.end() && it->second != nullptr) {
        return it->second;
    }
    auto provider = Microsoft::WRL::ComPtr<HomeClearUiaProvider>(new HomeClearUiaProvider(this, pane, clearKind));
    homeClears_[key] = provider;
    return provider;
}

Microsoft::WRL::ComPtr<TreemapGroupUiaProvider> ColumnViewUiaProvider::TreemapGroupElement() {
    if (!Alive() || treemapSurface_ == nullptr) return nullptr;
    if (treemapGroup_ == nullptr) {
        treemapGroup_ = Microsoft::WRL::ComPtr<TreemapGroupUiaProvider>(new TreemapGroupUiaProvider(this));
    }
    return treemapGroup_;
}

Microsoft::WRL::ComPtr<TreemapTileUiaProvider> ColumnViewUiaProvider::TreemapTileElement(uint64_t scopeHash, uint64_t idHash) {
    if (!Alive() || treemapSurface_ == nullptr) return nullptr;
    const uint64_t key = UiaCombineKey(scopeHash, idHash);
    auto it = treemapTiles_.find(key);
    if (it != treemapTiles_.end() && it->second != nullptr) {
        return it->second;
    }
    // Bounded, deterministic cache: on overflow clear the whole tile cache.
    if (treemapTileCacheSize_ >= kMaxCachedTreemapTiles) {
        treemapTiles_.clear();
        treemapTileCacheSize_ = 0;
    }
    auto provider = Microsoft::WRL::ComPtr<TreemapTileUiaProvider>(
        new TreemapTileUiaProvider(this, scopeHash, idHash));
    ++treemapTileCacheSize_;
    treemapTiles_[key] = provider;
    return provider;
}

// --- cache invalidation ----------------------------------------------------

void ColumnViewUiaProvider::InvalidateColumn(int pane, uint64_t pathHash) {
    const uint64_t key = UiaCombineKey(static_cast<uint64_t>(pane), pathHash);
    columns_.erase(key);
    statuses_.erase(key);
    // Item providers know their own (pane, pathHash); purge by identity.
    // Correctness never depends on this (items self-verify), it is memory
    // hygiene only.
    for (auto it = items_.begin(); it != items_.end();) {
        if (it->second != nullptr && it->second->Pane() == pane &&
            it->second->PathHash() == pathHash) {
            it = items_.erase(it);
        } else {
            ++it;
        }
    }
    itemCacheSize_ = items_.size();
}

void ColumnViewUiaProvider::InvalidatePane(int pane) {
    for (auto it = columns_.begin(); it != columns_.end();) {
        if (it->second != nullptr && it->second->Pane() == pane) it = columns_.erase(it);
        else ++it;
    }
    for (auto it = statuses_.begin(); it != statuses_.end();) {
        if (it->second != nullptr && it->second->Pane() == pane) it = statuses_.erase(it);
        else ++it;
    }
    for (auto it = items_.begin(); it != items_.end();) {
        if (it->second != nullptr && it->second->Pane() == pane) it = items_.erase(it);
        else ++it;
    }
    itemCacheSize_ = items_.size();
    InvalidateHome(pane);
}

void ColumnViewUiaProvider::InvalidateHome(int pane) {
    for (auto it = homeSections_.begin(); it != homeSections_.end();) {
        if (it->second != nullptr && it->second->Pane() == pane) it = homeSections_.erase(it);
        else ++it;
    }
    for (auto it = homeItems_.begin(); it != homeItems_.end();) {
        if (it->second != nullptr && it->second->Pane() == pane) it = homeItems_.erase(it);
        else ++it;
    }
    for (auto it = homeClears_.begin(); it != homeClears_.end();) {
        if (it->second != nullptr && it->second->Pane() == pane) it = homeClears_.erase(it);
        else ++it;
    }
    homeItemCacheSize_ = homeItems_.size();
    homeGroups_[static_cast<size_t>(pane)].Reset();
}

void ColumnViewUiaProvider::InvalidateTreemap() {
    treemapTiles_.clear();
    treemapTileCacheSize_ = 0;
    treemapGroup_.Reset();
}

void ColumnViewUiaProvider::InvalidateAll() {
    columns_.clear();
    items_.clear();
    statuses_.clear();
    homeSections_.clear();
    homeItems_.clear();
    homeClears_.clear();
    homeGroups_[0].Reset();
    homeGroups_[1].Reset();
    panes_[0].Reset();
    panes_[1].Reset();
    group_.Reset();
    itemCacheSize_ = 0;
    homeItemCacheSize_ = 0;
    InvalidateTreemap();
}

void ColumnViewUiaProvider::ReplaceFragment() {
    if (fragmentState_ != nullptr) {
        fragmentState_->alive = false;
    }
    fragmentState_ = std::make_shared<UiaOwnerState>();
    InvalidateAll();
}

// --- events ----------------------------------------------------------------

void ColumnViewUiaProvider::SinkThunk(const UiaEventNotification& notification, void* context) {
    auto* self = static_cast<ColumnViewUiaProvider*>(context);
    if (self != nullptr) {
        self->HandleNotification(notification);
    }
}

void ColumnViewUiaProvider::HandleNotification(const UiaEventNotification& notification) {
    if (!Alive() || surface_ == nullptr) return;
    switch (notification.kind) {
        case UiaEventKind::FocusChanged:
            NotifyFocusChanged();
            return;
        case UiaEventKind::SelectionInvalidated: {
            if (notification.pane < 0 || notification.column < 0) return;
            auto column = ColumnElement(notification.pane, notification.column);
            if (column != nullptr) {
                UiaProviderBase::RaiseSelectionInvalidated(column.Get());
            }
            return;
        }
        case UiaEventKind::ElementAddedToSelection:
        case UiaEventKind::ElementRemovedFromSelection: {
            if (notification.pane < 0 || notification.column < 0 || notification.row < 0) return;
            auto item = ItemElement(notification.pane, notification.column, notification.row);
            if (item != nullptr) {
                if (notification.kind == UiaEventKind::ElementAddedToSelection) {
                    UiaProviderBase::RaiseElementAddedToSelection(item.Get());
                } else {
                    UiaProviderBase::RaiseElementRemovedFromSelection(item.Get());
                }
            }
            return;
        }
        case UiaEventKind::StructureChanged:
        case UiaEventKind::StructureBulkRemoved:
        case UiaEventKind::StructureBulkAdded: {
            const StructureChangeType changeType =
                notification.kind == UiaEventKind::StructureBulkRemoved
                    ? StructureChangeType_ChildrenBulkRemoved
                    : (notification.kind == UiaEventKind::StructureBulkAdded
                           ? StructureChangeType_ChildrenBulkAdded
                           : StructureChangeType_ChildrenReordered);
            if (notification.treemap) {
                // semantic-treemap-uia 30.x: scope changed / snapshot
                // rebuild / filter changed / opened / closed. Providers of
                // the old structure fail stale; surviving identities are
                // re-minted by the new scope.
                auto treemapGroup = TreemapGroupElement();
                if (treemapGroup != nullptr) {
                    UiaProviderBase::RaiseStructureChanged(treemapGroup.Get(), changeType, nullptr);
                }
                InvalidateTreemap();
                return;
            }
            if (notification.group) {
                auto group = GroupElement();
                if (group != nullptr) {
                    UiaProviderBase::RaiseStructureChanged(group.Get(), changeType, nullptr);
                }
                // The event is raised against the old group while it is still
                // valid. Afterwards the complete custom fragment is replaced;
                // a same-index Pane.1 from a later toggle must not revive an
                // element held by the client from this generation.
                ReplaceFragment();
                return;
            }
            if (notification.home) {
                auto home = HomeGroupElement(notification.pane);
                if (home != nullptr) {
                    UiaProviderBase::RaiseStructureChanged(home.Get(), changeType, nullptr);
                }
                InvalidateHome(notification.pane);
                return;
            }
            if (notification.pane < 0) return;
            auto pane = PaneElement(notification.pane);
            if (pane != nullptr) {
                UiaProviderBase::RaiseStructureChanged(pane.Get(), changeType, nullptr);
            }
            if (notification.column >= 0) {
                uint64_t pathHash = 0;
                if (ResolveColumnIdentity(notification.pane, notification.column, pathHash)) {
                    InvalidateColumn(notification.pane, pathHash);
                }
            } else {
                InvalidatePane(notification.pane);
            }
            return;
        }
        case UiaEventKind::Invoked: {
            if (notification.treemap) {
                auto tile = TreemapTileElement(notification.treemapScopeHash, notification.treemapTileHash);
                if (tile != nullptr) {
                    UiaProviderBase::RaiseInvoked(tile.Get());
                }
                return;
            }
            if (notification.home) {
                auto item = HomeItemElement(notification.pane, notification.homeIndex);
                if (item != nullptr) {
                    UiaProviderBase::RaiseInvoked(item.Get());
                }
                return;
            }
            if (notification.pane < 0 || notification.column < 0 || notification.row < 0) return;
            auto item = ItemElement(notification.pane, notification.column, notification.row);
            if (item != nullptr) {
                UiaProviderBase::RaiseInvoked(item.Get());
            }
            return;
        }
        case UiaEventKind::PropertyChanged: {
            if (notification.treemap) {
                // aggregate completion: same semantic tile, updated
                // metadata / ItemStatus / bounds. PropertyChanged on the
                // surviving provider; the cache entry stays valid.
                auto tile = TreemapTileElement(notification.treemapScopeHash, notification.treemapTileHash);
                if (tile != nullptr) {
                    VARIANT oldValue{};
                    VARIANT newValue{};
                    VariantInit(&oldValue);
                    VariantInit(&newValue);
                    SetVariantString(&oldValue, notification.propertyOldText);
                    SetVariantString(&newValue, notification.propertyNewText);
                    UiaProviderBase::RaisePropertyChanged(tile.Get(), notification.propertyId,
                                                          oldValue, newValue);
                    VariantClear(&oldValue);
                    VariantClear(&newValue);
                }
                return;
            }
            if (!notification.home) return;
            auto item = HomeItemElement(notification.pane, notification.homeIndex);
            if (item != nullptr) {
                VARIANT oldValue{};
                VARIANT newValue{};
                VariantInit(&oldValue);
                VariantInit(&newValue);
                SetVariantBool(&oldValue, notification.propertyOldValue);
                SetVariantBool(&newValue, notification.propertyNewValue);
                UiaProviderBase::RaisePropertyChanged(item.Get(), notification.propertyId,
                                                      oldValue, newValue);
                VariantClear(&oldValue);
                VariantClear(&newValue);
            }
            return;
        }
    }
}

void ColumnViewUiaProvider::NotifyFocusChanged() {
    if (!Alive() || surface_ == nullptr) return;
    // The treemap has no focus model; do not raise focus on the obscured
    // Column View while it is active.
    if (treemapSurface_ != nullptr && treemapSurface_->TreemapActive()) return;
    auto focused = FocusElement();
    if (focused != nullptr) {
        UiaProviderBase::RaiseFocusChanged(focused.Get());
    }
}

// ---------------------------------------------------------------------------
// ColumnViewGroupUiaProvider
// ---------------------------------------------------------------------------

ColumnViewGroupUiaProvider::ColumnViewGroupUiaProvider(ColumnViewUiaProvider* owner)
    : UiaProviderBase(owner != nullptr ? owner->FragmentState() : nullptr), owner_(owner) {}

STDMETHODIMP ColumnViewGroupUiaProvider::GetPatternProvider(PATTERNID /*patternId*/, IUnknown** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    return S_OK;
}

STDMETHODIMP ColumnViewGroupUiaProvider::GetPropertyValue(PROPERTYID propertyId, VARIANT* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    VariantInit(retVal);
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    switch (propertyId) {
        case UIA_ControlTypePropertyId:
            SetVariantIntProperty(retVal, UIA_GroupControlTypeId);
            return S_OK;
        case UIA_NamePropertyId:
            SetVariantString(retVal, L"Column View");
            return S_OK;
        case UIA_AutomationIdPropertyId:
            SetVariantString(retVal, L"ColumnView");
            return S_OK;
        case UIA_IsKeyboardFocusablePropertyId:
            SetVariantBool(retVal, true);
            return S_OK;
        case UIA_IsEnabledPropertyId:
            SetVariantBool(retVal, true);
            return S_OK;
        case UIA_IsControlElementPropertyId:
        case UIA_IsContentElementPropertyId:
            SetVariantBool(retVal, true);
            return S_OK;
        case UIA_IsOffscreenPropertyId:
            SetVariantBool(retVal, false);
            return S_OK;
        case UIA_RuntimeIdPropertyId:
            SetVariantRuntimeId(retVal, UiaGroupRuntimeId());
            return S_OK;
        default:
            return S_OK;
    }
}

STDMETHODIMP ColumnViewGroupUiaProvider::Navigate(NavigateDirection direction, IRawElementProviderFragment** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
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
        case NavigateDirection_FirstChild:
        case NavigateDirection_LastChild: {
            const int paneCount = owner_->Surface()->UiaPaneCount();
            AppendProviderChildQueryDiagnostic(
                direction == NavigateDirection_FirstChild ? L"group-first-child" : L"group-last-child",
                paneCount);
            if (paneCount <= 0) return S_OK;
            const int pane = direction == NavigateDirection_LastChild ? paneCount - 1 : 0;
            auto paneElement = owner_->PaneElement(pane);
            if (paneElement != nullptr) {
                *retVal = paneElement.Detach();
            }
            return S_OK;
        }
        default:
            return S_OK;
    }
}

STDMETHODIMP ColumnViewGroupUiaProvider::GetRuntimeId(SAFEARRAY** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    if (!Alive()) return kElementNotAvailable;
    *retVal = MakeIntArray(UiaGroupRuntimeId());
    return S_OK;
}

STDMETHODIMP ColumnViewGroupUiaProvider::get_BoundingRectangle(UiaRect* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    const UiaViewGeometry geometry = owner_->Geometry();
    const RECT rect = UiaRectToScreen(D2D1::RectF(0.0f, 0.0f, geometry.widthDip, geometry.heightDip), geometry);
    retVal->left = static_cast<double>(rect.left);
    retVal->top = static_cast<double>(rect.top);
    retVal->width = static_cast<double>(rect.right - rect.left);
    retVal->height = static_cast<double>(rect.bottom - rect.top);
    return S_OK;
}

STDMETHODIMP ColumnViewGroupUiaProvider::GetEmbeddedFragmentRoots(SAFEARRAY** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    return S_OK;
}

STDMETHODIMP ColumnViewGroupUiaProvider::SetFocus() noexcept {
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive()) return kElementNotAvailable;
    const HWND hwnd = owner_->Hwnd();
    if (hwnd != nullptr && IsWindow(hwnd)) {
        ::SetFocus(hwnd);
    }
    return S_OK;
}

STDMETHODIMP ColumnViewGroupUiaProvider::get_FragmentRoot(IRawElementProviderFragmentRoot** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive()) return kElementNotAvailable;
    return owner_->QueryInterface(IID_IRawElementProviderFragmentRoot, reinterpret_cast<void**>(retVal));
}

// ---------------------------------------------------------------------------
// PaneUiaProvider
// ---------------------------------------------------------------------------

PaneUiaProvider::PaneUiaProvider(ColumnViewUiaProvider* owner, int pane)
    : UiaProviderBase(owner != nullptr ? owner->FragmentState() : nullptr), owner_(owner), pane_(pane) {}

STDMETHODIMP PaneUiaProvider::GetPatternProvider(PATTERNID patternId, IUnknown** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    if (patternId == UIA_ScrollPatternId) {
        return QueryInterface(IID_IScrollProvider, reinterpret_cast<void**>(retVal));
    }
    return S_OK;
}

STDMETHODIMP PaneUiaProvider::GetPropertyValue(PROPERTYID propertyId, VARIANT* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    VariantInit(retVal);
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    if (pane_ >= owner_->Surface()->UiaPaneCount()) {
        return kElementNotAvailable; // pane 1 after dual-pane was disabled
    }
    switch (propertyId) {
        case UIA_ControlTypePropertyId:
            SetVariantIntProperty(retVal, UIA_PaneControlTypeId);
            return S_OK;
        case UIA_NamePropertyId:
            SetVariantString(retVal, pane_ == 0 ? L"Pane 1" : L"Pane 2");
            return S_OK;
        case UIA_AutomationIdPropertyId:
            if (pane_ == 1) {
                AppendProviderChildQueryDiagnostic(L"pane1-automation-id", owner_->Surface()->UiaPaneCount());
            }
            SetVariantString(retVal, pane_ == 0 ? L"Pane.0" : L"Pane.1");
            return S_OK;
        case UIA_IsKeyboardFocusablePropertyId:
            SetVariantBool(retVal, true);
            return S_OK;
        case UIA_HasKeyboardFocusPropertyId: {
            // The pane owns keyboard focus only when it is the active pane,
            // the window really has focus, and no row carries the focus.
            const UiaViewGeometry geometry = owner_->Geometry();
            UiaSurface* paneSurface = owner_->Surface();
            const int focusedRow =
                paneSurface->UiaFocusedRow(pane_, paneSurface->UiaFocusedColumn(pane_));
            SetVariantBool(retVal, paneSurface->UiaActivePane() == pane_ &&
                                       geometry.keyboardFocus && focusedRow < 0);
            return S_OK;
        }
        case UIA_IsEnabledPropertyId:
            SetVariantBool(retVal, true);
            return S_OK;
        case UIA_IsControlElementPropertyId:
        case UIA_IsContentElementPropertyId:
            SetVariantBool(retVal, true);
            return S_OK;
        case UIA_IsOffscreenPropertyId:
            SetVariantBool(retVal, false);
            return S_OK;
        case UIA_RuntimeIdPropertyId:
            SetVariantRuntimeId(retVal, UiaPaneRuntimeId(pane_));
            return S_OK;
        case UIA_BoundingRectanglePropertyId: {
            const UiaViewGeometry geometry = owner_->Geometry();
            const float paneWidth = geometry.widthDip / static_cast<float>(owner_->Surface()->UiaPaneCount());
            const D2D1_RECT_F rectDip = UiaPaneRectDip(static_cast<float>(pane_) * paneWidth, paneWidth, geometry.heightDip);
            SetVariantRect(retVal, UiaRectToScreen(rectDip, geometry));
            return S_OK;
        }
        default:
            return S_OK;
    }
}

STDMETHODIMP PaneUiaProvider::Navigate(NavigateDirection direction, IRawElementProviderFragment** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    UiaSurface* surface = owner_->Surface();
    if (direction == NavigateDirection_NextSibling || direction == NavigateDirection_PreviousSibling) {
        AppendProviderChildQueryDiagnostic(
            direction == NavigateDirection_NextSibling ? L"pane-next-sibling" : L"pane-previous-sibling",
            surface->UiaPaneCount());
    }
    switch (direction) {
        case NavigateDirection_Parent: {
            auto group = owner_->GroupElement();
            if (group != nullptr) {
                *retVal = group.Detach();
            }
            return S_OK;
        }
        case NavigateDirection_NextSibling: {
            if (pane_ + 1 < surface->UiaPaneCount()) {
                AppendProviderChildQueryDiagnostic(L"pane-next-sibling-returned", surface->UiaPaneCount());
                auto next = owner_->PaneElement(pane_ + 1);
                if (next != nullptr) {
                    *retVal = next.Detach();
                }
            }
            return S_OK;
        }
        case NavigateDirection_PreviousSibling: {
            if (pane_ > 0) {
                auto previous = owner_->PaneElement(pane_ - 1);
                if (previous != nullptr) {
                    *retVal = previous.Detach();
                }
            }
            return S_OK;
        }
        case NavigateDirection_FirstChild:
        case NavigateDirection_LastChild: {
            if (surface->UiaPaneIsHome(pane_)) {
                auto home = owner_->HomeGroupElement(pane_);
                if (home != nullptr) {
                    *retVal = home.Detach();
                }
                return S_OK;
            }
            const int columnCount = surface->UiaColumnCount(pane_);
            if (columnCount <= 0) return S_OK;
            const int column = direction == NavigateDirection_FirstChild ? 0 : columnCount - 1;
            auto columnElement = owner_->ColumnElement(pane_, column);
            if (columnElement != nullptr) {
                *retVal = columnElement.Detach();
            }
            return S_OK;
        }
    }
    return S_OK;
}

STDMETHODIMP PaneUiaProvider::GetRuntimeId(SAFEARRAY** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    if (!Alive()) return kElementNotAvailable;
    *retVal = MakeIntArray(UiaPaneRuntimeId(pane_));
    return S_OK;
}

STDMETHODIMP PaneUiaProvider::get_BoundingRectangle(UiaRect* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    const UiaViewGeometry geometry = owner_->Geometry();
    const float paneWidth = geometry.widthDip / static_cast<float>(owner_->Surface()->UiaPaneCount());
    const D2D1_RECT_F rectDip = UiaPaneRectDip(static_cast<float>(pane_) * paneWidth, paneWidth, geometry.heightDip);
    const RECT rect = UiaRectToScreen(rectDip, geometry);
    retVal->left = static_cast<double>(rect.left);
    retVal->top = static_cast<double>(rect.top);
    retVal->width = static_cast<double>(rect.right - rect.left);
    retVal->height = static_cast<double>(rect.bottom - rect.top);
    return S_OK;
}

STDMETHODIMP PaneUiaProvider::GetEmbeddedFragmentRoots(SAFEARRAY** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    return S_OK;
}

STDMETHODIMP PaneUiaProvider::SetFocus() noexcept {
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    owner_->Surface()->UiaActivatePane(pane_);
    return S_OK;
}

STDMETHODIMP PaneUiaProvider::get_FragmentRoot(IRawElementProviderFragmentRoot** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive()) return kElementNotAvailable;
    return owner_->QueryInterface(IID_IRawElementProviderFragmentRoot, reinterpret_cast<void**>(retVal));
}

namespace {
// Scroll amount -> DIP delta for a pane dimension.
float ScrollDeltaDip(enum ScrollAmount amount, float dimension) {
    switch (amount) {
        case ScrollAmount_SmallIncrement: return 0.05f * dimension;
        case ScrollAmount_LargeIncrement: return 0.9f * dimension;
        default: return 0.0f;
    }
}

bool ScrollPercentInRange(double percent) {
    return percent >= 0.0 && percent <= 100.0;
}
} // namespace

STDMETHODIMP PaneUiaProvider::Scroll(enum ScrollAmount horizontalAmount, enum ScrollAmount verticalAmount) noexcept {
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    UiaSurface* surface = owner_->Surface();
    const UiaViewGeometry geometry = owner_->Geometry();
    const float paneWidth = geometry.widthDip / static_cast<float>(surface->UiaPaneCount());
    float deltaX = 0.0f;
    float deltaY = 0.0f;
    if (horizontalAmount != ScrollAmount_NoAmount) {
        if (!surface->UiaPaneIsHome(pane_)) {
            deltaX = ScrollDeltaDip(horizontalAmount, paneWidth);
        }
    }
    if (verticalAmount != ScrollAmount_NoAmount) {
        deltaY = ScrollDeltaDip(verticalAmount, geometry.heightDip);
    }
    surface->UiaScrollPane(pane_, deltaX, deltaY);
    return S_OK;
}

STDMETHODIMP PaneUiaProvider::SetScrollPercent(double horizontalPercent, double verticalPercent) noexcept {
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    UiaSurface* surface = owner_->Surface();
    const UiaViewGeometry geometry = owner_->Geometry();
    const float paneWidth = geometry.widthDip / static_cast<float>(surface->UiaPaneCount());
    const float hScroll = UiaPaneScrollDip(geometry, pane_);

    float deltaX = 0.0f;
    if (horizontalPercent != UIA_ScrollPatternNoScroll) {
        if (!ScrollPercentInRange(horizontalPercent)) return E_INVALIDARG;
        if (!surface->UiaPaneIsHome(pane_)) {
            const int columnCount = surface->UiaColumnCount(pane_);
            const float maxScroll = UiaMaxHorizontalScrollDip(columnCount, paneWidth);
            const float target = static_cast<float>(horizontalPercent / 100.0) * maxScroll;
            deltaX = target - hScroll;
        }
    }
    float deltaY = 0.0f;
    if (verticalPercent != UIA_ScrollPatternNoScroll) {
        if (!ScrollPercentInRange(verticalPercent)) return E_INVALIDARG;
        const float vScroll = surface->UiaVerticalScroll(pane_);
        float maxScroll = 0.0f;
        if (surface->UiaPaneIsHome(pane_)) {
            maxScroll = UiaMaxHomeScrollDip(surface->UiaHomeScrollInfoOf(pane_).contentHeightDip,
                                            geometry.heightDip);
        } else {
            const int column = surface->UiaFocusedColumn(pane_);
            const UiaColumnInfo info = surface->UiaColumnInfoOf(pane_, column);
            maxScroll = UiaMaxVerticalScrollDip(info.itemCount, geometry.heightDip);
        }
        const float target = static_cast<float>(verticalPercent / 100.0) * maxScroll;
        deltaY = target - vScroll;
    }
    surface->UiaScrollPane(pane_, deltaX, deltaY);
    return S_OK;
}

STDMETHODIMP PaneUiaProvider::get_HorizontalScrollPercent(double* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = UIA_ScrollPatternNoScroll;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    UiaSurface* surface = owner_->Surface();
    if (surface->UiaPaneIsHome(pane_)) return S_OK;
    const UiaViewGeometry geometry = owner_->Geometry();
    const float paneWidth = geometry.widthDip / static_cast<float>(surface->UiaPaneCount());
    const float maxScroll = UiaMaxHorizontalScrollDip(surface->UiaColumnCount(pane_), paneWidth);
    if (maxScroll <= 0.0f) return S_OK;
    const float hScroll = UiaPaneScrollDip(geometry, pane_);
    *retVal = static_cast<double>(hScroll / maxScroll * 100.0);
    return S_OK;
}

STDMETHODIMP PaneUiaProvider::get_VerticalScrollPercent(double* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = UIA_ScrollPatternNoScroll;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    UiaSurface* surface = owner_->Surface();
    const UiaViewGeometry geometry = owner_->Geometry();
    float maxScroll = 0.0f;
    if (surface->UiaPaneIsHome(pane_)) {
        maxScroll = UiaMaxHomeScrollDip(surface->UiaHomeScrollInfoOf(pane_).contentHeightDip,
                                        geometry.heightDip);
    } else {
        const int column = surface->UiaFocusedColumn(pane_);
        if (column < 0 || column >= surface->UiaColumnCount(pane_)) return S_OK;
        maxScroll = UiaMaxVerticalScrollDip(surface->UiaColumnInfoOf(pane_, column).itemCount,
                                            geometry.heightDip);
    }
    if (maxScroll <= 0.0f) return S_OK;
    *retVal = static_cast<double>(surface->UiaVerticalScroll(pane_) / maxScroll * 100.0);
    return S_OK;
}

STDMETHODIMP PaneUiaProvider::get_HorizontalViewSize(double* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = 100.0;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    UiaSurface* surface = owner_->Surface();
    if (surface->UiaPaneIsHome(pane_)) return S_OK;
    const UiaViewGeometry geometry = owner_->Geometry();
    const float paneWidth = geometry.widthDip / static_cast<float>(surface->UiaPaneCount());
    const float contentWidth = static_cast<float>(surface->UiaColumnCount(pane_)) * UiMetrics::kColumnWidth;
    if (contentWidth <= 0.0f) return S_OK;
    *retVal = static_cast<double>((std::min)(100.0f, paneWidth / contentWidth * 100.0f));
    return S_OK;
}

STDMETHODIMP PaneUiaProvider::get_VerticalViewSize(double* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = 100.0;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    UiaSurface* surface = owner_->Surface();
    const UiaViewGeometry geometry = owner_->Geometry();
    const float visibleHeight = (std::max)(0.0f, geometry.heightDip - UiMetrics::kBadgeHeight);
    float contentHeight = 0.0f;
    if (surface->UiaPaneIsHome(pane_)) {
        contentHeight = surface->UiaHomeScrollInfoOf(pane_).contentHeightDip;
    } else {
        const int column = surface->UiaFocusedColumn(pane_);
        if (column < 0 || column >= surface->UiaColumnCount(pane_)) return S_OK;
        contentHeight = static_cast<float>(surface->UiaColumnInfoOf(pane_, column).itemCount) * UiaBrowseRowHeightDip();
    }
    if (contentHeight <= 0.0f) return S_OK;
    *retVal = static_cast<double>((std::min)(100.0, visibleHeight / contentHeight * 100.0));
    return S_OK;
}

STDMETHODIMP PaneUiaProvider::get_HorizontallyScrollable(BOOL* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = FALSE;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    UiaSurface* surface = owner_->Surface();
    if (surface->UiaPaneIsHome(pane_)) return S_OK;
    const UiaViewGeometry geometry = owner_->Geometry();
    const float paneWidth = geometry.widthDip / static_cast<float>(surface->UiaPaneCount());
    *retVal = UiaMaxHorizontalScrollDip(surface->UiaColumnCount(pane_), paneWidth) > 0.0f ? TRUE : FALSE;
    return S_OK;
}

STDMETHODIMP PaneUiaProvider::get_VerticallyScrollable(BOOL* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = FALSE;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    UiaSurface* surface = owner_->Surface();
    const UiaViewGeometry geometry = owner_->Geometry();
    float maxScroll = 0.0f;
    if (surface->UiaPaneIsHome(pane_)) {
        maxScroll = UiaMaxHomeScrollDip(surface->UiaHomeScrollInfoOf(pane_).contentHeightDip,
                                        geometry.heightDip);
    } else {
        const int column = surface->UiaFocusedColumn(pane_);
        if (column < 0 || column >= surface->UiaColumnCount(pane_)) return S_OK;
        maxScroll = UiaMaxVerticalScrollDip(surface->UiaColumnInfoOf(pane_, column).itemCount,
                                            geometry.heightDip);
    }
    *retVal = maxScroll > 0.0f ? TRUE : FALSE;
    return S_OK;
}

// ---------------------------------------------------------------------------
// ColumnUiaProvider
// ---------------------------------------------------------------------------

ColumnUiaProvider::ColumnUiaProvider(ColumnViewUiaProvider* owner, int pane, int column, uint64_t pathHash)
    : UiaProviderBase(owner != nullptr ? owner->FragmentState() : nullptr),
      owner_(owner), pane_(pane), column_(column), pathHash_(pathHash) {}

STDMETHODIMP ColumnUiaProvider::GetPatternProvider(PATTERNID patternId, IUnknown** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    if (patternId == UIA_SelectionPatternId) {
        return QueryInterface(IID_ISelectionProvider, reinterpret_cast<void**>(retVal));
    }
    if (patternId == UIA_ItemContainerPatternId) {
        return QueryInterface(IID_IItemContainerProvider, reinterpret_cast<void**>(retVal));
    }
    return S_OK;
}

STDMETHODIMP ColumnUiaProvider::GetPropertyValue(PROPERTYID propertyId, VARIANT* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    VariantInit(retVal);
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    UiaSurface* surface = owner_->Surface();
    const UiaColumnInfo info = surface->UiaColumnInfoOf(pane_, column_);
    if (UiaHashWide(info.path) != pathHash_) {
        return kElementNotAvailable; // the column at this index changed identity
    }
    switch (propertyId) {
        case UIA_ControlTypePropertyId:
            SetVariantIntProperty(retVal, UIA_ListControlTypeId);
            return S_OK;
        case UIA_NamePropertyId:
            SetVariantString(retVal, info.displayPath.empty()
                                         ? (info.path.empty() ? std::wstring(L"This PC") : info.path)
                                         : info.displayPath);
            return S_OK;
        case UIA_AutomationIdPropertyId:
            SetVariantString(retVal, L"Pane." + std::to_wstring(pane_) + L".Column." + std::to_wstring(column_));
            return S_OK;
        case UIA_IsKeyboardFocusablePropertyId:
            SetVariantBool(retVal, true);
            return S_OK;
        case UIA_HasKeyboardFocusPropertyId: {
            const bool focused = surface->UiaActivePane() == pane_ &&
                                 surface->UiaFocusedColumn(pane_) == column_ &&
                                 surface->UiaFocusedRow(pane_, column_) < 0 &&
                                 owner_->Geometry().keyboardFocus;
            SetVariantBool(retVal, focused);
            return S_OK;
        }
        case UIA_IsEnabledPropertyId:
            SetVariantBool(retVal, true);
            return S_OK;
        case UIA_IsControlElementPropertyId:
        case UIA_IsContentElementPropertyId:
            SetVariantBool(retVal, true);
            return S_OK;
        case UIA_IsOffscreenPropertyId: {
            const UiaViewGeometry geometry = owner_->Geometry();
            const float paneWidth = geometry.widthDip / static_cast<float>(surface->UiaPaneCount());
            const float hScroll = UiaPaneScrollDip(geometry, pane_);
            const D2D1_RECT_F rectDip = UiaColumnRectDip(
                column_, static_cast<float>(pane_) * paneWidth, paneWidth, hScroll, geometry.heightDip);
            SetVariantBool(retVal, UiaRectOffscreen(rectDip, static_cast<float>(pane_) * paneWidth,
                                                    paneWidth, geometry.heightDip));
            return S_OK;
        }
        case UIA_BoundingRectanglePropertyId: {
            const UiaViewGeometry geometry = owner_->Geometry();
            const float paneWidth = geometry.widthDip / static_cast<float>(surface->UiaPaneCount());
            const float hScroll = UiaPaneScrollDip(geometry, pane_);
            const D2D1_RECT_F rectDip = UiaColumnRectDip(
                column_, static_cast<float>(pane_) * paneWidth, paneWidth, hScroll, geometry.heightDip);
            SetVariantRect(retVal, UiaRectToScreen(rectDip, geometry));
            return S_OK;
        }
        case UIA_RuntimeIdPropertyId:
            SetVariantRuntimeId(retVal, UiaColumnRuntimeId(pane_, pathHash_));
            return S_OK;
        default:
            return S_OK;
    }
}

STDMETHODIMP ColumnUiaProvider::Navigate(NavigateDirection direction, IRawElementProviderFragment** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    UiaSurface* surface = owner_->Surface();
    const UiaColumnInfo info = surface->UiaColumnInfoOf(pane_, column_);
    if (UiaHashWide(info.path) != pathHash_) {
        return kElementNotAvailable;
    }
    const bool hasStatus = info.status != UiaColumnStatus::Current && !info.statusText.empty();
    switch (direction) {
        case NavigateDirection_Parent: {
            auto pane = owner_->PaneElement(pane_);
            if (pane != nullptr) {
                *retVal = pane.Detach();
            }
            return S_OK;
        }
        case NavigateDirection_NextSibling: {
            if (column_ + 1 < surface->UiaColumnCount(pane_)) {
                auto next = owner_->ColumnElement(pane_, column_ + 1);
                if (next != nullptr) {
                    *retVal = next.Detach();
                }
            }
            return S_OK;
        }
        case NavigateDirection_PreviousSibling: {
            if (column_ > 0) {
                auto previous = owner_->ColumnElement(pane_, column_ - 1);
                if (previous != nullptr) {
                    *retVal = previous.Detach();
                }
            }
            return S_OK;
        }
        case NavigateDirection_FirstChild: {
            if (hasStatus) {
                auto status = owner_->StatusElement(pane_, column_);
                if (status != nullptr) {
                    *retVal = status.Detach();
                }
                return S_OK;
            }
            if (info.itemCount > 0) {
                auto item = owner_->ItemElement(pane_, column_, 0);
                if (item != nullptr) {
                    *retVal = item.Detach();
                }
            }
            return S_OK;
        }
        case NavigateDirection_LastChild: {
            if (info.itemCount > 0) {
                auto item = owner_->ItemElement(pane_, column_, info.itemCount - 1);
                if (item != nullptr) {
                    *retVal = item.Detach();
                }
            } else if (hasStatus) {
                auto status = owner_->StatusElement(pane_, column_);
                if (status != nullptr) {
                    *retVal = status.Detach();
                }
            }
            return S_OK;
        }
    }
    return S_OK;
}

STDMETHODIMP ColumnUiaProvider::GetRuntimeId(SAFEARRAY** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    if (!Alive()) return kElementNotAvailable;
    *retVal = MakeIntArray(UiaColumnRuntimeId(pane_, pathHash_));
    return S_OK;
}

STDMETHODIMP ColumnUiaProvider::get_BoundingRectangle(UiaRect* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    UiaSurface* surface = owner_->Surface();
    const UiaColumnInfo info = surface->UiaColumnInfoOf(pane_, column_);
    if (UiaHashWide(info.path) != pathHash_) {
        return kElementNotAvailable;
    }
    const UiaViewGeometry geometry = owner_->Geometry();
    const float paneWidth = geometry.widthDip / static_cast<float>(surface->UiaPaneCount());
    const float hScroll = UiaPaneScrollDip(geometry, pane_);
    const D2D1_RECT_F rectDip = UiaColumnRectDip(
        column_, static_cast<float>(pane_) * paneWidth, paneWidth, hScroll, geometry.heightDip);
    const RECT rect = UiaRectToScreen(rectDip, geometry);
    retVal->left = static_cast<double>(rect.left);
    retVal->top = static_cast<double>(rect.top);
    retVal->width = static_cast<double>(rect.right - rect.left);
    retVal->height = static_cast<double>(rect.bottom - rect.top);
    return S_OK;
}

STDMETHODIMP ColumnUiaProvider::GetEmbeddedFragmentRoots(SAFEARRAY** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    return S_OK;
}

STDMETHODIMP ColumnUiaProvider::SetFocus() noexcept {
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    owner_->Surface()->UiaSetColumnFocus(pane_, column_);
    return S_OK;
}

STDMETHODIMP ColumnUiaProvider::get_FragmentRoot(IRawElementProviderFragmentRoot** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive()) return kElementNotAvailable;
    return owner_->QueryInterface(IID_IRawElementProviderFragmentRoot, reinterpret_cast<void**>(retVal));
}

STDMETHODIMP ColumnUiaProvider::GetSelection(SAFEARRAY** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    UiaSurface* surface = owner_->Surface();
    const UiaColumnInfo info = surface->UiaColumnInfoOf(pane_, column_);
    if (UiaHashWide(info.path) != pathHash_) {
        return kElementNotAvailable;
    }
    std::vector<IRawElementProviderSimple*> selected;
    selected.reserve(static_cast<size_t>(info.itemCount));
    for (int row = 0; row < info.itemCount; ++row) {
        const UiaItemInfo item = surface->UiaItemInfoOf(pane_, column_, row);
        if (item.selected) {
            const auto provider = owner_->ItemElement(pane_, column_, row);
            if (provider != nullptr) {
                selected.push_back(provider.Get());
            }
        }
    }
    *retVal = MakeUnknownArray(std::move(selected));
    return S_OK;
}

STDMETHODIMP ColumnUiaProvider::get_CanSelectMultiple(BOOL* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    if (!Alive()) return kElementNotAvailable;
    *retVal = TRUE; // the real model supports Ctrl/Shift multi-select
    return S_OK;
}

STDMETHODIMP ColumnUiaProvider::get_IsSelectionRequired(BOOL* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    if (!Alive()) return kElementNotAvailable;
    *retVal = FALSE;
    return S_OK;
}

STDMETHODIMP ColumnUiaProvider::FindItemByProperty(IRawElementProviderSimple* startAfter,
                                                   PROPERTYID propertyId, VARIANT value,
                                                   IRawElementProviderSimple** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    if (propertyId != UIA_NamePropertyId) {
        return UIA_E_INVALIDOPERATION; // items expose no other findable identity
    }
    UiaSurface* surface = owner_->Surface();
    const UiaColumnInfo info = surface->UiaColumnInfoOf(pane_, column_);
    if (UiaHashWide(info.path) != pathHash_) {
        return kElementNotAvailable;
    }
    int startRow = RowOfItemProvider(owner_, pane_, column_, startAfter);
    for (int row = startRow + 1; row < info.itemCount; ++row) {
        const UiaItemInfo item = surface->UiaItemInfoOf(pane_, column_, row);
        if (NameMatches(item.name, value)) {
            const auto provider = owner_->ItemElement(pane_, column_, row);
            if (provider != nullptr) {
                return provider->QueryInterface(IID_IRawElementProviderSimple, reinterpret_cast<void**>(retVal));
            }
            return S_OK;
        }
    }
    return S_OK;
}

// ---------------------------------------------------------------------------
// ColumnItemUiaProvider
// ---------------------------------------------------------------------------

ColumnItemUiaProvider::ColumnItemUiaProvider(ColumnViewUiaProvider* owner, int pane, int column, int row,
                                             uint64_t pathHash, uint64_t idHash)
    : UiaProviderBase(owner != nullptr ? owner->FragmentState() : nullptr),
      owner_(owner), pane_(pane), column_(column), row_(row),
      pathHash_(pathHash), idHash_(idHash) {}

bool ColumnItemUiaProvider::Resolve(UiaItemInfo& item, bool& statusLineShown) const {
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return false;
    }
    UiaSurface* surface = owner_->Surface();
    if (column_ < 0 || column_ >= surface->UiaColumnCount(pane_)) {
        return false;
    }
    const UiaColumnInfo column = surface->UiaColumnInfoOf(pane_, column_);
    if (UiaHashWide(column.path) != pathHash_) {
        return false; // a different column now occupies this index
    }
    if (row_ < 0 || row_ >= column.itemCount) {
        return false;
    }
    item = surface->UiaItemInfoOf(pane_, column_, row_);
    statusLineShown = column.status == UiaColumnStatus::Loading;
    // Never retarget: the current occupant of this row must still be the
    // item this element was created for.
    return UiaItemIdentityHash(item.volumeRowId, item.fileIdLow, item.fileIdHigh, item.fullPath) == idHash_;
}

STDMETHODIMP ColumnItemUiaProvider::GetPatternProvider(PATTERNID patternId, IUnknown** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
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

STDMETHODIMP ColumnItemUiaProvider::GetPropertyValue(PROPERTYID propertyId, VARIANT* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    VariantInit(retVal);
    UiaItemInfo item;
    bool statusLine = false;
    if (!Resolve(item, statusLine)) {
        return kElementNotAvailable;
    }
    UiaSurface* surface = owner_->Surface();
    switch (propertyId) {
        case UIA_ControlTypePropertyId:
            SetVariantIntProperty(retVal, UIA_ListItemControlTypeId);
            return S_OK;
        case UIA_NamePropertyId:
            SetVariantString(retVal, item.name);
            return S_OK;
        case UIA_HelpTextPropertyId:
            SetVariantString(retVal, item.displayFullPath.empty() ? item.fullPath : item.displayFullPath);
            return S_OK;
        case UIA_FullDescriptionPropertyId:
            SetVariantString(retVal, item.isDirectory ? std::wstring(L"Folder")
                                                      : L"File, " + UiaSizeText(item.sizeBytes));
            return S_OK;
        case UIA_IsKeyboardFocusablePropertyId:
            SetVariantBool(retVal, true);
            return S_OK;
        case UIA_HasKeyboardFocusPropertyId: {
            const bool focused = item.focused &&
                                 surface->UiaActivePane() == pane_ &&
                                 surface->UiaFocusedColumn(pane_) == column_ &&
                                 owner_->Geometry().keyboardFocus;
            SetVariantBool(retVal, focused);
            return S_OK;
        }
        case UIA_IsEnabledPropertyId:
            SetVariantBool(retVal, true);
            return S_OK;
        case UIA_IsControlElementPropertyId:
        case UIA_IsContentElementPropertyId:
            SetVariantBool(retVal, true);
            return S_OK;
        case UIA_IsOffscreenPropertyId: {
            const UiaViewGeometry geometry = owner_->Geometry();
            const float paneWidth = geometry.widthDip / static_cast<float>(surface->UiaPaneCount());
            const float hScroll = UiaPaneScrollDip(geometry, pane_);
            const D2D1_RECT_F rectDip = UiaItemRectDip(
                column_, row_, static_cast<float>(pane_) * paneWidth, paneWidth, hScroll,
                surface->UiaVerticalScroll(pane_), statusLine, geometry.heightDip);
            SetVariantBool(retVal, UiaRectOffscreen(rectDip, static_cast<float>(pane_) * paneWidth,
                                                    paneWidth, geometry.heightDip));
            return S_OK;
        }
        case UIA_BoundingRectanglePropertyId: {
            const UiaViewGeometry geometry = owner_->Geometry();
            const float paneWidth = geometry.widthDip / static_cast<float>(surface->UiaPaneCount());
            const float hScroll = UiaPaneScrollDip(geometry, pane_);
            const D2D1_RECT_F rectDip = UiaItemRectDip(
                column_, row_, static_cast<float>(pane_) * paneWidth, paneWidth, hScroll,
                surface->UiaVerticalScroll(pane_), statusLine, geometry.heightDip);
            SetVariantRect(retVal, UiaRectToScreen(rectDip, geometry));
            return S_OK;
        }
        case UIA_RuntimeIdPropertyId: {
            const uint64_t idHash = UiaItemIdentityHash(item.volumeRowId, item.fileIdLow,
                                                        item.fileIdHigh, item.fullPath);
            SetVariantRuntimeId(retVal, UiaItemRuntimeId(pane_, pathHash_, idHash));
            return S_OK;
        }
        default:
            return S_OK;
    }
}

STDMETHODIMP ColumnItemUiaProvider::Navigate(NavigateDirection direction, IRawElementProviderFragment** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    UiaItemInfo item;
    bool statusLine = false;
    if (!Resolve(item, statusLine)) {
        return kElementNotAvailable;
    }
    switch (direction) {
        case NavigateDirection_Parent: {
            auto column = owner_->ColumnElement(pane_, column_);
            if (column != nullptr) {
                *retVal = column.Detach();
            }
            return S_OK;
        }
        case NavigateDirection_NextSibling: {
            auto next = owner_->ItemElement(pane_, column_, row_ + 1);
            if (next != nullptr) {
                *retVal = next.Detach();
            }
            return S_OK;
        }
        case NavigateDirection_PreviousSibling: {
            if (row_ > 0) {
                auto previous = owner_->ItemElement(pane_, column_, row_ - 1);
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

STDMETHODIMP ColumnItemUiaProvider::GetRuntimeId(SAFEARRAY** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    UiaItemInfo item;
    bool statusLine = false;
    if (!Resolve(item, statusLine)) {
        return kElementNotAvailable;
    }
    const uint64_t idHash = UiaItemIdentityHash(item.volumeRowId, item.fileIdLow,
                                                item.fileIdHigh, item.fullPath);
    *retVal = MakeIntArray(UiaItemRuntimeId(pane_, pathHash_, idHash));
    return S_OK;
}

STDMETHODIMP ColumnItemUiaProvider::get_BoundingRectangle(UiaRect* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    UiaItemInfo item;
    bool statusLine = false;
    if (!Resolve(item, statusLine)) {
        return kElementNotAvailable;
    }
    UiaSurface* surface = owner_->Surface();
    const UiaViewGeometry geometry = owner_->Geometry();
    const float paneWidth = geometry.widthDip / static_cast<float>(surface->UiaPaneCount());
    const float hScroll = UiaPaneScrollDip(geometry, pane_);
    const D2D1_RECT_F rectDip = UiaItemRectDip(
        column_, row_, static_cast<float>(pane_) * paneWidth, paneWidth, hScroll,
        surface->UiaVerticalScroll(pane_), statusLine, geometry.heightDip);
    const RECT rect = UiaRectToScreen(rectDip, geometry);
    retVal->left = static_cast<double>(rect.left);
    retVal->top = static_cast<double>(rect.top);
    retVal->width = static_cast<double>(rect.right - rect.left);
    retVal->height = static_cast<double>(rect.bottom - rect.top);
    return S_OK;
}

STDMETHODIMP ColumnItemUiaProvider::GetEmbeddedFragmentRoots(SAFEARRAY** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    return S_OK;
}

STDMETHODIMP ColumnItemUiaProvider::SetFocus() noexcept {
    UiaItemInfo item;
    bool statusLine = false;
    if (!Resolve(item, statusLine)) {
        return kElementNotAvailable;
    }
    owner_->Surface()->UiaSetItemFocus(pane_, column_, row_);
    return S_OK;
}

STDMETHODIMP ColumnItemUiaProvider::get_FragmentRoot(IRawElementProviderFragmentRoot** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive()) return kElementNotAvailable;
    return owner_->QueryInterface(IID_IRawElementProviderFragmentRoot, reinterpret_cast<void**>(retVal));
}

STDMETHODIMP ColumnItemUiaProvider::Select() noexcept {
    UiaItemInfo item;
    bool statusLine = false;
    if (!Resolve(item, statusLine)) {
        return kElementNotAvailable;
    }
    owner_->Surface()->UiaSelectItem(pane_, column_, row_);
    return S_OK;
}

STDMETHODIMP ColumnItemUiaProvider::AddToSelection() noexcept {
    UiaItemInfo item;
    bool statusLine = false;
    if (!Resolve(item, statusLine)) {
        return kElementNotAvailable;
    }
    if (!item.selected) {
        owner_->Surface()->UiaToggleItem(pane_, column_, row_);
    }
    return S_OK;
}

STDMETHODIMP ColumnItemUiaProvider::RemoveFromSelection() noexcept {
    UiaItemInfo item;
    bool statusLine = false;
    if (!Resolve(item, statusLine)) {
        return kElementNotAvailable;
    }
    if (item.selected) {
        owner_->Surface()->UiaToggleItem(pane_, column_, row_);
    }
    return S_OK;
}

STDMETHODIMP ColumnItemUiaProvider::get_IsSelected(BOOL* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = FALSE;
    UiaItemInfo item;
    bool statusLine = false;
    if (!Resolve(item, statusLine)) {
        return kElementNotAvailable;
    }
    *retVal = item.selected ? TRUE : FALSE;
    return S_OK;
}

STDMETHODIMP ColumnItemUiaProvider::get_SelectionContainer(IRawElementProviderSimple** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    auto column = owner_->ColumnElement(pane_, column_);
    if (column != nullptr) {
        return column->QueryInterface(IID_IRawElementProviderSimple, reinterpret_cast<void**>(retVal));
    }
    return S_OK;
}

STDMETHODIMP ColumnItemUiaProvider::Invoke() noexcept {
    UiaItemInfo item;
    bool statusLine = false;
    if (!Resolve(item, statusLine)) {
        return kElementNotAvailable;
    }
    owner_->Surface()->UiaActivateItem(pane_, column_, row_);
    UiaProviderBase::RaiseInvoked(this);
    return S_OK;
}

STDMETHODIMP ColumnItemUiaProvider::ScrollIntoView() noexcept {
    UiaItemInfo item;
    bool statusLine = false;
    if (!Resolve(item, statusLine)) {
        return kElementNotAvailable;
    }
    owner_->Surface()->UiaScrollItemIntoView(pane_, column_, row_);
    return S_OK;
}

// ---------------------------------------------------------------------------
// ColumnStatusUiaProvider
// ---------------------------------------------------------------------------

ColumnStatusUiaProvider::ColumnStatusUiaProvider(ColumnViewUiaProvider* owner, int pane, int column)
    : UiaProviderBase(owner != nullptr ? owner->FragmentState() : nullptr),
      owner_(owner), pane_(pane), column_(column) {}

STDMETHODIMP ColumnStatusUiaProvider::GetPatternProvider(PATTERNID /*patternId*/, IUnknown** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive()) return kElementNotAvailable;
    return S_OK;
}

STDMETHODIMP ColumnStatusUiaProvider::GetPropertyValue(PROPERTYID propertyId, VARIANT* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    VariantInit(retVal);
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    UiaSurface* surface = owner_->Surface();
    const UiaColumnInfo info = surface->UiaColumnInfoOf(pane_, column_);
    if (info.statusText.empty()) {
        return kElementNotAvailable; // the status line is no longer drawn
    }
    switch (propertyId) {
        case UIA_ControlTypePropertyId:
            SetVariantIntProperty(retVal, UIA_TextControlTypeId);
            return S_OK;
        case UIA_NamePropertyId:
            SetVariantString(retVal, info.statusText);
            return S_OK;
        case UIA_IsKeyboardFocusablePropertyId:
            SetVariantBool(retVal, false);
            return S_OK;
        case UIA_IsEnabledPropertyId:
            SetVariantBool(retVal, true);
            return S_OK;
        case UIA_IsControlElementPropertyId:
        case UIA_IsContentElementPropertyId:
            SetVariantBool(retVal, true);
            return S_OK;
        case UIA_IsOffscreenPropertyId:
        case UIA_BoundingRectanglePropertyId: {
            const UiaViewGeometry geometry = owner_->Geometry();
            const float paneWidth = geometry.widthDip / static_cast<float>(surface->UiaPaneCount());
            const float hScroll = UiaPaneScrollDip(geometry, pane_);
            const float x = static_cast<float>(pane_) * paneWidth + static_cast<float>(column_) * UiMetrics::kColumnWidth - hScroll;
            const float top = info.status == UiaColumnStatus::Loading
                ? UiMetrics::kBadgeHeight + 4.0f : UiMetrics::kBadgeHeight + 8.0f;
            const float height = info.status == UiaColumnStatus::Loading ? 24.0f : 40.0f;
            const D2D1_RECT_F rectDip = D2D1::RectF(x + 8.0f, top, x + UiMetrics::kColumnWidth - 8.0f, top + height);
            if (propertyId == UIA_IsOffscreenPropertyId) {
                SetVariantBool(retVal, UiaRectOffscreen(rectDip, static_cast<float>(pane_) * paneWidth,
                                                        paneWidth, geometry.heightDip));
            } else {
                SetVariantRect(retVal, UiaRectToScreen(rectDip, geometry));
            }
            return S_OK;
        }
        case UIA_RuntimeIdPropertyId: {
            uint64_t pathHash = 0;
            if (owner_->ResolveColumnIdentity(pane_, column_, pathHash)) {
                SetVariantRuntimeId(retVal, UiaStatusRuntimeId(pane_, pathHash));
            }
            return S_OK;
        }
        default:
            return S_OK;
    }
}

STDMETHODIMP ColumnStatusUiaProvider::Navigate(NavigateDirection direction, IRawElementProviderFragment** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    if (direction == NavigateDirection_Parent) {
        auto column = owner_->ColumnElement(pane_, column_);
        if (column != nullptr) {
            *retVal = column.Detach();
        }
        return S_OK;
    }
    if (direction == NavigateDirection_NextSibling) {
        // The status line precedes the column's items (drawn above them).
        const UiaColumnInfo info = owner_->Surface()->UiaColumnInfoOf(pane_, column_);
        if (info.itemCount > 0) {
            auto item = owner_->ItemElement(pane_, column_, 0);
            if (item != nullptr) {
                *retVal = item.Detach();
            }
        }
    }
    return S_OK;
}

STDMETHODIMP ColumnStatusUiaProvider::GetRuntimeId(SAFEARRAY** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    uint64_t pathHash = 0;
    if (Alive() && owner_ != nullptr && owner_->SurfaceAlive() && owner_->ResolveColumnIdentity(pane_, column_, pathHash)) {
         *retVal = MakeIntArray(UiaStatusRuntimeId(pane_, pathHash));
        return S_OK;
    }
    return kElementNotAvailable;
}

STDMETHODIMP ColumnStatusUiaProvider::get_BoundingRectangle(UiaRect* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    UiaSurface* surface = owner_->Surface();
    const UiaColumnInfo info = surface->UiaColumnInfoOf(pane_, column_);
    if (info.statusText.empty()) {
        return kElementNotAvailable;
    }
    const UiaViewGeometry geometry = owner_->Geometry();
    const float paneWidth = geometry.widthDip / static_cast<float>(surface->UiaPaneCount());
    const float hScroll = UiaPaneScrollDip(geometry, pane_);
    const float x = static_cast<float>(pane_) * paneWidth + static_cast<float>(column_) * UiMetrics::kColumnWidth - hScroll;
    const float top = info.status == UiaColumnStatus::Loading
        ? UiMetrics::kBadgeHeight + 4.0f : UiMetrics::kBadgeHeight + 8.0f;
    const float height = info.status == UiaColumnStatus::Loading ? 24.0f : 40.0f;
    const D2D1_RECT_F rectDip = D2D1::RectF(x + 8.0f, top, x + UiMetrics::kColumnWidth - 8.0f, top + height);
    const RECT rect = UiaRectToScreen(rectDip, geometry);
    retVal->left = static_cast<double>(rect.left);
    retVal->top = static_cast<double>(rect.top);
    retVal->width = static_cast<double>(rect.right - rect.left);
    retVal->height = static_cast<double>(rect.bottom - rect.top);
    return S_OK;
}

STDMETHODIMP ColumnStatusUiaProvider::GetEmbeddedFragmentRoots(SAFEARRAY** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    return S_OK;
}

STDMETHODIMP ColumnStatusUiaProvider::SetFocus() noexcept {
    return S_OK; // informational text; not focusable
}

STDMETHODIMP ColumnStatusUiaProvider::get_FragmentRoot(IRawElementProviderFragmentRoot** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive()) return kElementNotAvailable;
    return owner_->QueryInterface(IID_IRawElementProviderFragmentRoot, reinterpret_cast<void**>(retVal));
}

// ---------------------------------------------------------------------------
// HomeGroupUiaProvider / HomeSectionUiaProvider / HomeItemUiaProvider /
// HomeClearUiaProvider
// ---------------------------------------------------------------------------

HomeGroupUiaProvider::HomeGroupUiaProvider(ColumnViewUiaProvider* owner, int pane)
    : UiaProviderBase(owner != nullptr ? owner->FragmentState() : nullptr), owner_(owner), pane_(pane) {}

STDMETHODIMP HomeGroupUiaProvider::GetPatternProvider(PATTERNID /*patternId*/, IUnknown** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive()) return kElementNotAvailable;
    return S_OK;
}

STDMETHODIMP HomeGroupUiaProvider::GetPropertyValue(PROPERTYID propertyId, VARIANT* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    VariantInit(retVal);
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    if (!owner_->Surface()->UiaPaneIsHome(pane_)) {
        return kElementNotAvailable; // the pane left Home mode
    }
    switch (propertyId) {
        case UIA_ControlTypePropertyId:
            SetVariantIntProperty(retVal, UIA_GroupControlTypeId);
            return S_OK;
        case UIA_NamePropertyId:
            SetVariantString(retVal, L"Home");
            return S_OK;
        case UIA_AutomationIdPropertyId:
            SetVariantString(retVal, L"Pane." + std::to_wstring(pane_) + L".Home");
            return S_OK;
        case UIA_IsKeyboardFocusablePropertyId:
            SetVariantBool(retVal, true);
            return S_OK;
        case UIA_IsEnabledPropertyId:
            SetVariantBool(retVal, true);
            return S_OK;
        case UIA_IsControlElementPropertyId:
        case UIA_IsContentElementPropertyId:
            SetVariantBool(retVal, true);
            return S_OK;
        case UIA_IsOffscreenPropertyId:
            SetVariantBool(retVal, false);
            return S_OK;
        case UIA_BoundingRectanglePropertyId: {
            const UiaViewGeometry geometry = owner_->Geometry();
            const float paneWidth = geometry.widthDip / static_cast<float>(owner_->Surface()->UiaPaneCount());
            const D2D1_RECT_F rectDip = UiaPaneRectDip(static_cast<float>(pane_) * paneWidth, paneWidth,
                                                       geometry.heightDip);
            SetVariantRect(retVal, UiaRectToScreen(rectDip, geometry));
            return S_OK;
        }
        case UIA_RuntimeIdPropertyId:
            SetVariantRuntimeId(retVal, UiaHomeRootRuntimeId(pane_));
            return S_OK;
        default:
            return S_OK;
    }
}

STDMETHODIMP HomeGroupUiaProvider::Navigate(NavigateDirection direction, IRawElementProviderFragment** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    UiaSurface* surface = owner_->Surface();
    if (!surface->UiaPaneIsHome(pane_)) {
        return kElementNotAvailable;
    }
    switch (direction) {
        case NavigateDirection_Parent: {
            auto pane = owner_->PaneElement(pane_);
            if (pane != nullptr) {
                *retVal = pane.Detach();
            }
            return S_OK;
        }
        case NavigateDirection_FirstChild:
        case NavigateDirection_LastChild: {
            const int sectionCount = surface->UiaHomeSectionCount(pane_);
            if (sectionCount <= 0) return S_OK;
            const int ordinal = direction == NavigateDirection_FirstChild ? 0 : sectionCount - 1;
            auto section = owner_->HomeSectionElement(pane_, ordinal);
            if (section != nullptr) {
                *retVal = section.Detach();
            }
            return S_OK;
        }
        default:
            return S_OK;
    }
}

STDMETHODIMP HomeGroupUiaProvider::GetRuntimeId(SAFEARRAY** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive()) return kElementNotAvailable;
    *retVal = MakeIntArray(UiaHomeRootRuntimeId(pane_));
    return S_OK;
}

STDMETHODIMP HomeGroupUiaProvider::get_BoundingRectangle(UiaRect* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    const UiaViewGeometry geometry = owner_->Geometry();
    const float paneWidth = geometry.widthDip / static_cast<float>(owner_->Surface()->UiaPaneCount());
    const D2D1_RECT_F rectDip = UiaPaneRectDip(static_cast<float>(pane_) * paneWidth, paneWidth,
                                               geometry.heightDip);
    const RECT rect = UiaRectToScreen(rectDip, geometry);
    retVal->left = static_cast<double>(rect.left);
    retVal->top = static_cast<double>(rect.top);
    retVal->width = static_cast<double>(rect.right - rect.left);
    retVal->height = static_cast<double>(rect.bottom - rect.top);
    return S_OK;
}

STDMETHODIMP HomeGroupUiaProvider::GetEmbeddedFragmentRoots(SAFEARRAY** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    return S_OK;
}

STDMETHODIMP HomeGroupUiaProvider::SetFocus() noexcept {
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    owner_->Surface()->UiaActivatePane(pane_);
    return S_OK;
}

STDMETHODIMP HomeGroupUiaProvider::get_FragmentRoot(IRawElementProviderFragmentRoot** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive()) return kElementNotAvailable;
    return owner_->QueryInterface(IID_IRawElementProviderFragmentRoot, reinterpret_cast<void**>(retVal));
}

HomeSectionUiaProvider::HomeSectionUiaProvider(ColumnViewUiaProvider* owner, int pane, int ordinal)
    : UiaProviderBase(owner != nullptr ? owner->FragmentState() : nullptr),
      owner_(owner), pane_(pane), ordinal_(ordinal) {}

namespace {
std::wstring HomeSectionTitle(int sectionKind) {
    switch (sectionKind) {
        case kUiaHomeSectionPinned: return L"Pinned";
        case kUiaHomeSectionRecentFolders: return L"Recent folders";
        case kUiaHomeSectionRecentSearches: return L"Recent searches";
        case kUiaHomeSectionCommonLocations: return L"Common locations";
        case kUiaHomeSectionQuickActions: return L"Quick actions";
        default: return L"";
    }
}

std::wstring HomeSectionAutomationId(int pane, int sectionKind) {
    std::wstring name = HomeSectionTitle(sectionKind);
    for (auto& ch : name) {
        if (ch == L' ') ch = L'.';
    }
    return L"Pane." + std::to_wstring(pane) + L".Home.Section." + name;
}
} // namespace

STDMETHODIMP HomeSectionUiaProvider::GetPatternProvider(PATTERNID /*patternId*/, IUnknown** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive()) return kElementNotAvailable;
    return S_OK;
}

STDMETHODIMP HomeSectionUiaProvider::GetPropertyValue(PROPERTYID propertyId, VARIANT* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    VariantInit(retVal);
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    UiaSurface* surface = owner_->Surface();
    if (ordinal_ >= surface->UiaHomeSectionCount(pane_)) {
        return kElementNotAvailable;
    }
    const UiaHomeSectionInfo section = surface->UiaHomeSectionInfoOf(pane_, ordinal_);
    switch (propertyId) {
        case UIA_ControlTypePropertyId:
            SetVariantIntProperty(retVal, UIA_GroupControlTypeId);
            return S_OK;
        case UIA_NamePropertyId:
            SetVariantString(retVal, HomeSectionTitle(section.sectionKind));
            return S_OK;
        case UIA_AutomationIdPropertyId:
            SetVariantString(retVal, HomeSectionAutomationId(pane_, section.sectionKind));
            return S_OK;
        case UIA_IsKeyboardFocusablePropertyId:
            SetVariantBool(retVal, false); // headings are not focus stops
            return S_OK;
        case UIA_IsEnabledPropertyId:
            SetVariantBool(retVal, true);
            return S_OK;
        case UIA_IsControlElementPropertyId:
        case UIA_IsContentElementPropertyId:
            SetVariantBool(retVal, true);
            return S_OK;
        case UIA_IsOffscreenPropertyId: {
            const UiaViewGeometry geometry = owner_->Geometry();
            const float paneWidth = geometry.widthDip / static_cast<float>(surface->UiaPaneCount());
            const float vScroll = surface->UiaVerticalScroll(pane_);
            const D2D1_RECT_F headerDip = UiaHomeHeaderRectDip(
                static_cast<float>(pane_) * paneWidth, section.headerX, section.headerY,
                section.headerWidth, section.headerHeight, vScroll);
            SetVariantBool(retVal, UiaRectOffscreen(headerDip, static_cast<float>(pane_) * paneWidth,
                                                    paneWidth, geometry.heightDip));
            return S_OK;
        }
        case UIA_BoundingRectanglePropertyId: {
            const UiaViewGeometry geometry = owner_->Geometry();
            const float paneWidth = geometry.widthDip / static_cast<float>(surface->UiaPaneCount());
            const float vScroll = surface->UiaVerticalScroll(pane_);
            const D2D1_RECT_F headerDip = UiaHomeHeaderRectDip(
                static_cast<float>(pane_) * paneWidth, section.headerX, section.headerY,
                section.headerWidth, section.headerHeight, vScroll);
            SetVariantRect(retVal, UiaRectToScreen(headerDip, geometry));
            return S_OK;
        }
        case UIA_RuntimeIdPropertyId:
            SetVariantRuntimeId(retVal, UiaHomeSectionRuntimeId(pane_, section.sectionKind, ordinal_));
            return S_OK;
        default:
            return S_OK;
    }
}

STDMETHODIMP HomeSectionUiaProvider::Navigate(NavigateDirection direction, IRawElementProviderFragment** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    UiaSurface* surface = owner_->Surface();
    if (ordinal_ >= surface->UiaHomeSectionCount(pane_)) {
        return kElementNotAvailable;
    }
    const UiaHomeSectionInfo section = surface->UiaHomeSectionInfoOf(pane_, ordinal_);
    const int sectionCount = surface->UiaHomeSectionCount(pane_);
    switch (direction) {
        case NavigateDirection_Parent: {
            auto home = owner_->HomeGroupElement(pane_);
            if (home != nullptr) {
                *retVal = home.Detach();
            }
            return S_OK;
        }
        case NavigateDirection_NextSibling: {
            if (ordinal_ + 1 < sectionCount) {
                auto next = owner_->HomeSectionElement(pane_, ordinal_ + 1);
                if (next != nullptr) {
                    *retVal = next.Detach();
                }
            }
            return S_OK;
        }
        case NavigateDirection_PreviousSibling: {
            if (ordinal_ > 0) {
                auto previous = owner_->HomeSectionElement(pane_, ordinal_ - 1);
                if (previous != nullptr) {
                    *retVal = previous.Detach();
                }
            }
            return S_OK;
        }
        case NavigateDirection_FirstChild: {
            if (section.clearAffordance) {
                const int clearKind = section.sectionKind == kUiaHomeSectionRecentFolders
                    ? kUiaHomeClearRecentFolders : kUiaHomeClearRecentSearches;
                auto clear = owner_->HomeClearElement(pane_, clearKind);
                if (clear != nullptr) {
                    *retVal = clear.Detach();
                }
                return S_OK;
            }
            if (section.itemCount > 0) {
                auto item = owner_->HomeItemElement(pane_, section.firstGlobalIndex);
                if (item != nullptr) {
                    *retVal = item.Detach();
                }
            }
            return S_OK;
        }
        case NavigateDirection_LastChild: {
            if (section.itemCount > 0) {
                auto item = owner_->HomeItemElement(
                    pane_, section.firstGlobalIndex + section.itemCount - 1);
                if (item != nullptr) {
                    *retVal = item.Detach();
                }
            } else if (section.clearAffordance) {
                const int clearKind = section.sectionKind == kUiaHomeSectionRecentFolders
                    ? kUiaHomeClearRecentFolders : kUiaHomeClearRecentSearches;
                auto clear = owner_->HomeClearElement(pane_, clearKind);
                if (clear != nullptr) {
                    *retVal = clear.Detach();
                }
            }
            return S_OK;
        }
    }
    return S_OK;
}

STDMETHODIMP HomeSectionUiaProvider::GetRuntimeId(SAFEARRAY** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    UiaSurface* surface = owner_->Surface();
    if (ordinal_ >= surface->UiaHomeSectionCount(pane_)) {
        return kElementNotAvailable;
    }
    const UiaHomeSectionInfo section = surface->UiaHomeSectionInfoOf(pane_, ordinal_);
    *retVal = MakeIntArray(UiaHomeSectionRuntimeId(pane_, section.sectionKind, ordinal_));
    return S_OK;
}

STDMETHODIMP HomeSectionUiaProvider::get_BoundingRectangle(UiaRect* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    UiaSurface* surface = owner_->Surface();
    if (ordinal_ >= surface->UiaHomeSectionCount(pane_)) {
        return kElementNotAvailable;
    }
    const UiaViewGeometry geometry = owner_->Geometry();
    const float paneWidth = geometry.widthDip / static_cast<float>(surface->UiaPaneCount());
    const float vScroll = surface->UiaVerticalScroll(pane_);
    const UiaHomeSectionInfo section = surface->UiaHomeSectionInfoOf(pane_, ordinal_);
    const D2D1_RECT_F headerDip = UiaHomeHeaderRectDip(
        static_cast<float>(pane_) * paneWidth, section.headerX, section.headerY,
        section.headerWidth, section.headerHeight, vScroll);
    const RECT rect = UiaRectToScreen(headerDip, geometry);
    retVal->left = static_cast<double>(rect.left);
    retVal->top = static_cast<double>(rect.top);
    retVal->width = static_cast<double>(rect.right - rect.left);
    retVal->height = static_cast<double>(rect.bottom - rect.top);
    return S_OK;
}

STDMETHODIMP HomeSectionUiaProvider::GetEmbeddedFragmentRoots(SAFEARRAY** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    return S_OK;
}

STDMETHODIMP HomeSectionUiaProvider::SetFocus() noexcept {
    return S_OK; // headings are not focus stops
}

STDMETHODIMP HomeSectionUiaProvider::get_FragmentRoot(IRawElementProviderFragmentRoot** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive()) return kElementNotAvailable;
    return owner_->QueryInterface(IID_IRawElementProviderFragmentRoot, reinterpret_cast<void**>(retVal));
}

HomeItemUiaProvider::HomeItemUiaProvider(ColumnViewUiaProvider* owner, int pane, int globalIndex,
                                         int sectionKind, int indexInSection, uint64_t valueHash)
    : UiaProviderBase(owner != nullptr ? owner->FragmentState() : nullptr),
      owner_(owner), pane_(pane), globalIndex_(globalIndex),
      sectionKind_(sectionKind), indexInSection_(indexInSection), valueHash_(valueHash) {}

bool HomeItemUiaProvider::Resolve(UiaHomeItemInfo& info) const {
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return false;
    }
    UiaSurface* surface = owner_->Surface();
    if (globalIndex_ < 0 || globalIndex_ >= surface->UiaHomeItemCount(pane_)) {
        return false;
    }
    info = surface->UiaHomeItemInfoOf(pane_, globalIndex_);
    return info.sectionKind == sectionKind_ && info.indexInSection == indexInSection_ &&
           UiaHashWide(info.value) == valueHash_;
}

STDMETHODIMP HomeItemUiaProvider::GetPatternProvider(PATTERNID patternId, IUnknown** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    UiaHomeItemInfo item;
    if (!Resolve(item)) {
        return kElementNotAvailable;
    }
    if (!item.enabled) {
        return S_OK; // disabled items expose no Invoke
    }
    if (patternId == UIA_InvokePatternId) {
        return QueryInterface(IID_IInvokeProvider, reinterpret_cast<void**>(retVal));
    }
    if (patternId == UIA_ScrollItemPatternId) {
        return QueryInterface(IID_IScrollItemProvider, reinterpret_cast<void**>(retVal));
    }
    return S_OK;
}

STDMETHODIMP HomeItemUiaProvider::GetPropertyValue(PROPERTYID propertyId, VARIANT* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    VariantInit(retVal);
    UiaHomeItemInfo item;
    if (!Resolve(item)) {
        return kElementNotAvailable;
    }
    UiaSurface* surface = owner_->Surface();
    switch (propertyId) {
        case UIA_ControlTypePropertyId:
            SetVariantIntProperty(retVal, item.sectionKind == kUiaHomeSectionQuickActions
                                              ? UIA_ButtonControlTypeId
                                              : UIA_ListItemControlTypeId);
            return S_OK;
        case UIA_NamePropertyId:
            SetVariantString(retVal, item.primary);
            return S_OK;
        case UIA_HelpTextPropertyId:
            if (!item.secondary.empty()) {
                SetVariantString(retVal, item.secondary);
            }
            return S_OK;
        case UIA_IsKeyboardFocusablePropertyId:
            SetVariantBool(retVal, item.enabled);
            return S_OK;
        case UIA_HasKeyboardFocusPropertyId: {
            const bool focused = item.enabled &&
                                 surface->UiaHomeFocusTarget(pane_) == kUiaHomeFocusItem &&
                                 surface->UiaHomeFocusIndex(pane_) == globalIndex_ &&
                                 owner_->Geometry().keyboardFocus;
            SetVariantBool(retVal, focused);
            return S_OK;
        }
        case UIA_IsEnabledPropertyId:
            SetVariantBool(retVal, item.enabled);
            return S_OK;
        case UIA_IsControlElementPropertyId:
        case UIA_IsContentElementPropertyId:
            SetVariantBool(retVal, true);
            return S_OK;
        case UIA_IsOffscreenPropertyId: {
            const UiaViewGeometry geometry = owner_->Geometry();
            const float paneWidth = geometry.widthDip / static_cast<float>(surface->UiaPaneCount());
            const D2D1_RECT_F rectDip = UiaHomeItemRectDip(
                static_cast<float>(pane_) * paneWidth, item.x, item.y, item.width, item.height,
                surface->UiaVerticalScroll(pane_));
            SetVariantBool(retVal, UiaRectOffscreen(rectDip, static_cast<float>(pane_) * paneWidth,
                                                    paneWidth, geometry.heightDip));
            return S_OK;
        }
        case UIA_BoundingRectanglePropertyId: {
            const UiaViewGeometry geometry = owner_->Geometry();
            const float paneWidth = geometry.widthDip / static_cast<float>(surface->UiaPaneCount());
            const D2D1_RECT_F rectDip = UiaHomeItemRectDip(
                static_cast<float>(pane_) * paneWidth, item.x, item.y, item.width, item.height,
                surface->UiaVerticalScroll(pane_));
            SetVariantRect(retVal, UiaRectToScreen(rectDip, geometry));
            return S_OK;
        }
        case UIA_RuntimeIdPropertyId: {
            const uint64_t valueHash = UiaHashWide(item.value);
            SetVariantRuntimeId(retVal, UiaHomeItemRuntimeId(pane_, item.sectionKind,
                                                             item.indexInSection, valueHash));
            return S_OK;
        }
        default:
            return S_OK;
    }
}

STDMETHODIMP HomeItemUiaProvider::Navigate(NavigateDirection direction, IRawElementProviderFragment** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    UiaHomeItemInfo item;
    if (!Resolve(item)) {
        return kElementNotAvailable;
    }
    UiaSurface* surface = owner_->Surface();
    const int sectionCount = surface->UiaHomeSectionCount(pane_);
    int myOrdinal = -1;
    for (int ordinal = 0; ordinal < sectionCount; ++ordinal) {
        const UiaHomeSectionInfo section = surface->UiaHomeSectionInfoOf(pane_, ordinal);
        if (globalIndex_ >= section.firstGlobalIndex &&
            globalIndex_ < section.firstGlobalIndex + section.itemCount) {
            myOrdinal = ordinal;
            break;
        }
    }
    if (myOrdinal < 0) {
        return kElementNotAvailable;
    }
    const UiaHomeSectionInfo section = surface->UiaHomeSectionInfoOf(pane_, myOrdinal);
    switch (direction) {
        case NavigateDirection_Parent: {
            auto sectionElement = owner_->HomeSectionElement(pane_, myOrdinal);
            if (sectionElement != nullptr) {
                *retVal = sectionElement.Detach();
            }
            return S_OK;
        }
        case NavigateDirection_NextSibling: {
            if (globalIndex_ + 1 < section.firstGlobalIndex + section.itemCount) {
                auto next = owner_->HomeItemElement(pane_, globalIndex_ + 1);
                if (next != nullptr) {
                    *retVal = next.Detach();
                }
            }
            return S_OK;
        }
        case NavigateDirection_PreviousSibling: {
            if (globalIndex_ > section.firstGlobalIndex) {
                auto previous = owner_->HomeItemElement(pane_, globalIndex_ - 1);
                if (previous != nullptr) {
                    *retVal = previous.Detach();
                }
            } else if (section.clearAffordance) {
                const int clearKind = section.sectionKind == kUiaHomeSectionRecentFolders
                    ? kUiaHomeClearRecentFolders : kUiaHomeClearRecentSearches;
                auto clear = owner_->HomeClearElement(pane_, clearKind);
                if (clear != nullptr) {
                    *retVal = clear.Detach();
                }
            }
            return S_OK;
        }
        default:
            return S_OK;
    }
}

STDMETHODIMP HomeItemUiaProvider::GetRuntimeId(SAFEARRAY** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    UiaHomeItemInfo item;
    if (!Resolve(item)) {
        return kElementNotAvailable;
    }
    *retVal = MakeIntArray(UiaHomeItemRuntimeId(pane_, item.sectionKind, item.indexInSection, UiaHashWide(item.value)));
    return S_OK;
}

STDMETHODIMP HomeItemUiaProvider::get_BoundingRectangle(UiaRect* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    UiaHomeItemInfo item;
    if (!Resolve(item)) {
        return kElementNotAvailable;
    }
    UiaSurface* surface = owner_->Surface();
    const UiaViewGeometry geometry = owner_->Geometry();
    const float paneWidth = geometry.widthDip / static_cast<float>(surface->UiaPaneCount());
    const D2D1_RECT_F rectDip = UiaHomeItemRectDip(
        static_cast<float>(pane_) * paneWidth, item.x, item.y, item.width, item.height,
        surface->UiaVerticalScroll(pane_));
    const RECT rect = UiaRectToScreen(rectDip, geometry);
    retVal->left = static_cast<double>(rect.left);
    retVal->top = static_cast<double>(rect.top);
    retVal->width = static_cast<double>(rect.right - rect.left);
    retVal->height = static_cast<double>(rect.bottom - rect.top);
    return S_OK;
}

STDMETHODIMP HomeItemUiaProvider::GetEmbeddedFragmentRoots(SAFEARRAY** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    return S_OK;
}

STDMETHODIMP HomeItemUiaProvider::SetFocus() noexcept {
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    UiaHomeItemInfo item;
    if (!Resolve(item)) {
        return kElementNotAvailable;
    }
    UiaSurface* surface = owner_->Surface();
    if (!item.enabled) {
        return E_FAIL; // disabled items are not focusable
    }
    surface->UiaSetHomeFocusItem(pane_, globalIndex_);
    return S_OK;
}

STDMETHODIMP HomeItemUiaProvider::get_FragmentRoot(IRawElementProviderFragmentRoot** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive()) return kElementNotAvailable;
    return owner_->QueryInterface(IID_IRawElementProviderFragmentRoot, reinterpret_cast<void**>(retVal));
}

STDMETHODIMP HomeItemUiaProvider::Invoke() noexcept {
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    UiaHomeItemInfo item;
    if (!Resolve(item)) {
        return kElementNotAvailable;
    }
    UiaSurface* surface = owner_->Surface();
    if (!item.enabled) {
        return E_FAIL;
    }
    surface->UiaActivateHomeItem(pane_, globalIndex_, false);
    UiaProviderBase::RaiseInvoked(this);
    return S_OK;
}

STDMETHODIMP HomeItemUiaProvider::ScrollIntoView() noexcept {
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    UiaHomeItemInfo item;
    if (!Resolve(item)) {
        return kElementNotAvailable;
    }
    UiaSurface* surface = owner_->Surface();
    surface->UiaScrollHomeItemIntoView(pane_, globalIndex_);
    return S_OK;
}

HomeClearUiaProvider::HomeClearUiaProvider(ColumnViewUiaProvider* owner, int pane, int clearKind)
    : UiaProviderBase(owner != nullptr ? owner->FragmentState() : nullptr),
      owner_(owner), pane_(pane), clearKind_(clearKind) {}

namespace {
int HomeClearSectionKind(int clearKind) {
    return clearKind == kUiaHomeClearRecentFolders
        ? kUiaHomeSectionRecentFolders : kUiaHomeSectionRecentSearches;
}

std::wstring HomeClearName(int clearKind) {
    return clearKind == kUiaHomeClearRecentFolders ? L"Clear recent folders"
                                                   : L"Clear recent searches";
}

// Locate the section that hosts a Clear affordance, or return a section with
// sectionKind == -1 when it is absent.
UiaHomeSectionInfo HomeClearSectionOf(UiaSurface* surface, int pane, int clearKind) {
    const int wantedSection = HomeClearSectionKind(clearKind);
    const int sectionCount = surface->UiaHomeSectionCount(pane);
    for (int ordinal = 0; ordinal < sectionCount; ++ordinal) {
        const UiaHomeSectionInfo section = surface->UiaHomeSectionInfoOf(pane, ordinal);
        if (section.sectionKind == wantedSection && section.clearAffordance) {
            return section;
        }
    }
    UiaHomeSectionInfo none;
    none.sectionKind = -1;
    return none;
}
} // namespace

STDMETHODIMP HomeClearUiaProvider::GetPatternProvider(PATTERNID patternId, IUnknown** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive()) return kElementNotAvailable;
    if (patternId == UIA_InvokePatternId) {
        return QueryInterface(IID_IInvokeProvider, reinterpret_cast<void**>(retVal));
    }
    return S_OK;
}

STDMETHODIMP HomeClearUiaProvider::GetPropertyValue(PROPERTYID propertyId, VARIANT* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    VariantInit(retVal);
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    UiaSurface* surface = owner_->Surface();
    const int wantedSection = HomeClearSectionKind(clearKind_);
    bool present = false;
    const int sectionCount = surface->UiaHomeSectionCount(pane_);
    for (int ordinal = 0; ordinal < sectionCount; ++ordinal) {
        const UiaHomeSectionInfo section = surface->UiaHomeSectionInfoOf(pane_, ordinal);
        if (section.sectionKind == wantedSection && section.clearAffordance) {
            present = true;
            break;
        }
    }
    if (!present) {
        return kElementNotAvailable; // the affordance disappeared
    }
    switch (propertyId) {
        case UIA_ControlTypePropertyId:
            SetVariantIntProperty(retVal, UIA_ButtonControlTypeId);
            return S_OK;
        case UIA_NamePropertyId:
            SetVariantString(retVal, HomeClearName(clearKind_));
            return S_OK;
        case UIA_AutomationIdPropertyId:
            SetVariantString(retVal, L"Pane." + std::to_wstring(pane_) + L".Home.Clear." +
                                         std::wstring(clearKind_ == kUiaHomeClearRecentFolders
                                                          ? L"RecentFolders" : L"RecentSearches"));
            return S_OK;
        case UIA_IsKeyboardFocusablePropertyId:
            SetVariantBool(retVal, true);
            return S_OK;
        case UIA_HasKeyboardFocusPropertyId: {
            const int focusTarget = clearKind_ == kUiaHomeClearRecentFolders
                ? kUiaHomeFocusClearRecentFolders : kUiaHomeFocusClearRecentSearches;
            const bool focused = surface->UiaHomeFocusTarget(pane_) == focusTarget &&
                                 owner_->Geometry().keyboardFocus;
            SetVariantBool(retVal, focused);
            return S_OK;
        }
        case UIA_IsEnabledPropertyId:
            SetVariantBool(retVal, true);
            return S_OK;
        case UIA_IsControlElementPropertyId:
        case UIA_IsContentElementPropertyId:
            SetVariantBool(retVal, true);
            return S_OK;
        case UIA_IsOffscreenPropertyId: {
            const UiaViewGeometry geometry = owner_->Geometry();
            const float paneWidth = geometry.widthDip / static_cast<float>(surface->UiaPaneCount());
            const UiaHomeSectionInfo section = HomeClearSectionOf(surface, pane_, clearKind_);
            if (section.sectionKind < 0) return kElementNotAvailable;
            const float vScroll = surface->UiaVerticalScroll(pane_);
            const D2D1_RECT_F rectDip = UiaHomeHeaderRectDip(
                static_cast<float>(pane_) * paneWidth,
                section.headerX + section.headerWidth - 44.0f, section.headerY,
                44.0f, section.headerHeight, vScroll);
            SetVariantBool(retVal, UiaRectOffscreen(rectDip, static_cast<float>(pane_) * paneWidth,
                                                    paneWidth, geometry.heightDip));
            return S_OK;
        }
        case UIA_BoundingRectanglePropertyId: {
            const UiaViewGeometry geometry = owner_->Geometry();
            const float paneWidth = geometry.widthDip / static_cast<float>(surface->UiaPaneCount());
            const UiaHomeSectionInfo section = HomeClearSectionOf(surface, pane_, clearKind_);
            if (section.sectionKind < 0) return kElementNotAvailable;
            const float vScroll = surface->UiaVerticalScroll(pane_);
            const D2D1_RECT_F rectDip = UiaHomeHeaderRectDip(
                static_cast<float>(pane_) * paneWidth,
                section.headerX + section.headerWidth - 44.0f, section.headerY,
                44.0f, section.headerHeight, vScroll);
            SetVariantRect(retVal, UiaRectToScreen(rectDip, geometry));
            return S_OK;
        }
        case UIA_RuntimeIdPropertyId:
            SetVariantRuntimeId(retVal, UiaHomeClearRuntimeId(pane_, wantedSection));
            return S_OK;
        default:
            return S_OK;
    }
}

UiaHomeSectionInfo HomeClearUiaProvider::HomeClearSection(UiaSurface* surface) const {
    return HomeClearSectionOf(surface, pane_, clearKind_);
}

STDMETHODIMP HomeClearUiaProvider::Navigate(NavigateDirection direction, IRawElementProviderFragment** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    UiaSurface* surface = owner_->Surface();
    if (direction == NavigateDirection_Parent) {
        const int sectionCount = surface->UiaHomeSectionCount(pane_);
        for (int ordinal = 0; ordinal < sectionCount; ++ordinal) {
            const UiaHomeSectionInfo section = surface->UiaHomeSectionInfoOf(pane_, ordinal);
            if (section.sectionKind == HomeClearSectionKind(clearKind_) && section.clearAffordance) {
                auto sectionElement = owner_->HomeSectionElement(pane_, ordinal);
                if (sectionElement != nullptr) {
                    *retVal = sectionElement.Detach();
                }
                return S_OK;
            }
        }
        return kElementNotAvailable;
    }
    if (direction == NavigateDirection_NextSibling) {
        // The Clear affordance precedes its section's items (header above
        // items), matching the focus-list order.
        const UiaHomeSectionInfo section = HomeClearSection(surface);
        if (section.sectionKind < 0 || section.itemCount <= 0) return S_OK;
        auto item = owner_->HomeItemElement(pane_, section.firstGlobalIndex);
        if (item != nullptr) {
            *retVal = item.Detach();
        }
        return S_OK;
    }
    return S_OK;
}

STDMETHODIMP HomeClearUiaProvider::GetRuntimeId(SAFEARRAY** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive()) return kElementNotAvailable;
    *retVal = MakeIntArray(UiaHomeClearRuntimeId(pane_, HomeClearSectionKind(clearKind_)));
    return S_OK;
}

STDMETHODIMP HomeClearUiaProvider::get_BoundingRectangle(UiaRect* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    UiaSurface* surface = owner_->Surface();
    const UiaViewGeometry geometry = owner_->Geometry();
    const float paneWidth = geometry.widthDip / static_cast<float>(surface->UiaPaneCount());
    const UiaHomeSectionInfo section = HomeClearSectionOf(surface, pane_, clearKind_);
    if (section.sectionKind < 0) return kElementNotAvailable;
    const float vScroll = surface->UiaVerticalScroll(pane_);
    const D2D1_RECT_F rectDip = UiaHomeHeaderRectDip(
        static_cast<float>(pane_) * paneWidth,
        section.headerX + section.headerWidth - 44.0f, section.headerY,
        44.0f, section.headerHeight, vScroll);
    const RECT rect = UiaRectToScreen(rectDip, geometry);
    retVal->left = static_cast<double>(rect.left);
    retVal->top = static_cast<double>(rect.top);
    retVal->width = static_cast<double>(rect.right - rect.left);
    retVal->height = static_cast<double>(rect.bottom - rect.top);
    return S_OK;
}

STDMETHODIMP HomeClearUiaProvider::GetEmbeddedFragmentRoots(SAFEARRAY** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    return S_OK;
}

STDMETHODIMP HomeClearUiaProvider::SetFocus() noexcept {
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    owner_->Surface()->UiaSetHomeFocusClear(pane_, clearKind_);
    return S_OK;
}

STDMETHODIMP HomeClearUiaProvider::get_FragmentRoot(IRawElementProviderFragmentRoot** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive()) return kElementNotAvailable;
    return owner_->QueryInterface(IID_IRawElementProviderFragmentRoot, reinterpret_cast<void**>(retVal));
}

STDMETHODIMP HomeClearUiaProvider::Invoke() noexcept {
    if (!Alive() || owner_ == nullptr || !owner_->SurfaceAlive() || owner_->Surface() == nullptr) {
        return kElementNotAvailable;
    }
    UiaSurface* surface = owner_->Surface();
    const UiaHomeSectionInfo section = HomeClearSectionOf(surface, pane_, clearKind_);
    if (section.sectionKind < 0) {
        return kElementNotAvailable;
    }
    surface->UiaActivateHomeClear(pane_, clearKind_);
    UiaProviderBase::RaiseInvoked(this);
    return S_OK;
}

} // namespace ffui
