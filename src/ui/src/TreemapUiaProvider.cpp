#include "TreemapUiaProvider.h"

#include <algorithm>
#include <cwctype>
#include <iomanip>
#include <sstream>

#include "ColumnViewUiaProvider.h"

namespace ffui {

namespace {

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
constexpr PROPERTYID kItemStatusId = UIA_ItemStatusPropertyId;
constexpr PROPERTYID kItemTypeId = UIA_ItemTypePropertyId;

constexpr HRESULT kElementNotAvailable = UIA_E_ELEMENTNOTAVAILABLE;

// Runtime-ID SAFEARRAY builder (mirrors the controller's; kept local so this
// translation unit stays independent of ColumnViewUiaProvider.cpp internals).
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

// The AutomationId of every tile: "Storage.Treemap.Tile.<16-hex identity>".
// Stable across refreshes; distinct for distinct semantic identities.
std::wstring TileAutomationId(uint64_t idHash) {
    std::wostringstream stream;
    stream << L"Storage.Treemap.Tile." << std::hex << std::setw(16) << std::setfill(L'0') << idHash;
    return stream.str();
}

// Case-insensitive ordinal match (UIA Name matches are case-insensitive).
bool NameMatches(const std::wstring& name, const VARIANT& value) {
    if (value.vt != VT_BSTR || value.bstrVal == nullptr) return false;
    const std::wstring expected(value.bstrVal, SysStringLen(value.bstrVal));
    return _wcsicmp(name.c_str(), expected.c_str()) == 0;
}

// Viewport-DIP rectangle for a normalized 0..100 treemap tile.
D2D1_RECT_F TileRectDip(const TreemapUiaItemInfo& info, const UiaViewGeometry& geometry) {
    const float widthDip = geometry.widthDip > 0.0f ? geometry.widthDip : 1.0f;
    const float heightDip = geometry.heightDip > 0.0f ? geometry.heightDip : 1.0f;
    return D2D1::RectF(info.rectX / 100.0f * widthDip,
                       info.rectY / 100.0f * heightDip,
                       (info.rectX + info.rectWidth) / 100.0f * widthDip,
                       (info.rectY + info.rectHeight) / 100.0f * heightDip);
}

} // namespace

// ---------------------------------------------------------------------------
// TreemapGroupUiaProvider
// ---------------------------------------------------------------------------

TreemapGroupUiaProvider::TreemapGroupUiaProvider(ColumnViewUiaProvider* owner)
    : UiaProviderBase(owner != nullptr ? owner->State() : nullptr), owner_(owner) {}

STDMETHODIMP TreemapGroupUiaProvider::GetPatternProvider(PATTERNID patternId, IUnknown** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || owner_ == nullptr || owner_->TreemapSurface() == nullptr) return kElementNotAvailable;
    if (patternId == UIA_ItemContainerPatternId) {
        return QueryInterface(IID_IItemContainerProvider, reinterpret_cast<void**>(retVal));
    }
    return S_OK;
}

STDMETHODIMP TreemapGroupUiaProvider::GetPropertyValue(PROPERTYID propertyId, VARIANT* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    VariantInit(retVal);
    if (!Alive() || owner_ == nullptr || owner_->TreemapSurface() == nullptr) return kElementNotAvailable;
    switch (propertyId) {
        case UIA_ControlTypePropertyId:
            UiaProviderBase::SetVariantInt(retVal, UIA_GroupControlTypeId);
            return S_OK;
        case UIA_AutomationIdPropertyId:
            UiaProviderBase::SetVariantString(retVal, L"Storage.Treemap");
            return S_OK;
        case UIA_NamePropertyId:
            UiaProviderBase::SetVariantString(retVal, L"Storage Treemap");
            return S_OK;
        case UIA_IsEnabledPropertyId:
            UiaProviderBase::SetVariantBool(retVal, true);
            return S_OK;
        case UIA_IsKeyboardFocusablePropertyId:
            // Honest: the treemap has no keyboard-focus model (design
            // semantic-treemap-uia 9.x); clients invoke tiles directly.
            UiaProviderBase::SetVariantBool(retVal, false);
            return S_OK;
        case UIA_IsControlElementPropertyId:
        case UIA_IsContentElementPropertyId:
            UiaProviderBase::SetVariantBool(retVal, true);
            return S_OK;
        case UIA_ProviderDescriptionPropertyId:
            UiaProviderBase::SetVariantString(retVal, L"FastFiles Storage Treemap provider");
            return S_OK;
        case UIA_ItemStatusPropertyId:
            UiaProviderBase::SetVariantString(retVal, L"");
            return S_OK;
        default:
            break;
    }
    return S_OK;
}

STDMETHODIMP TreemapGroupUiaProvider::Navigate(NavigateDirection direction, IRawElementProviderFragment** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || owner_ == nullptr || owner_->TreemapSurface() == nullptr) return kElementNotAvailable;
    TreemapUiaSurface* surface = owner_->TreemapSurface();
    switch (direction) {
        case NavigateDirection_Parent: {
            Microsoft::WRL::ComPtr<IRawElementProviderFragment> root;
            if (owner_ != nullptr && SUCCEEDED(owner_->QueryInterface(IID_PPV_ARGS(&root)))) {
                *retVal = root.Detach();
            }
            return S_OK;
        }
        case NavigateDirection_FirstChild: {
            if (surface->TreemapTileCount() > 0) {
                const TreemapUiaItemInfo info = surface->TreemapTileInfo(0);
                auto tile = owner_->TreemapTileElement(info.scopeHash, info.identityHash);
                if (tile != nullptr) {
                    *retVal = tile.Detach();
                }
            }
            return S_OK;
        }
        case NavigateDirection_LastChild: {
            const int count = surface->TreemapTileCount();
            if (count > 0) {
                const TreemapUiaItemInfo info = surface->TreemapTileInfo(count - 1);
                auto tile = owner_->TreemapTileElement(info.scopeHash, info.identityHash);
                if (tile != nullptr) {
                    *retVal = tile.Detach();
                }
            }
            return S_OK;
        }
        default:
            return S_OK; // no siblings of the group
    }
}

STDMETHODIMP TreemapGroupUiaProvider::GetRuntimeId(SAFEARRAY** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || owner_ == nullptr || owner_->TreemapSurface() == nullptr) return kElementNotAvailable;
    *retVal = MakeIntArray(UiaTreemapGroupRuntimeId(owner_->TreemapSurface()->TreemapScopeHash()));
    return S_OK;
}

STDMETHODIMP TreemapGroupUiaProvider::get_BoundingRectangle(UiaRect* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    if (!Alive() || owner_ == nullptr || owner_->TreemapSurface() == nullptr) return kElementNotAvailable;
    const UiaViewGeometry geometry = owner_->Geometry();
    const RECT rect = UiaRectToScreen(D2D1::RectF(0.0f, 0.0f, geometry.widthDip, geometry.heightDip), geometry);
    retVal->left = static_cast<double>(rect.left);
    retVal->top = static_cast<double>(rect.top);
    retVal->width = static_cast<double>(rect.right - rect.left);
    retVal->height = static_cast<double>(rect.bottom - rect.top);
    return S_OK;
}

STDMETHODIMP TreemapGroupUiaProvider::GetEmbeddedFragmentRoots(SAFEARRAY** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    return S_OK;
}

STDMETHODIMP TreemapGroupUiaProvider::SetFocus() noexcept {
    // The treemap has no focus model; activating the host window is the
    // honest fallback (focusing the window keeps focus moves predictable).
    if (!Alive() || owner_ == nullptr) return kElementNotAvailable;
    HWND hwnd = owner_->Hwnd();
    if (hwnd != nullptr && IsWindow(hwnd)) {
        ::SetFocus(hwnd);
    }
    return S_OK;
}

STDMETHODIMP TreemapGroupUiaProvider::get_FragmentRoot(IRawElementProviderFragmentRoot** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || owner_ == nullptr) return kElementNotAvailable;
    return owner_->QueryInterface(IID_IRawElementProviderFragmentRoot, reinterpret_cast<void**>(retVal));
}

STDMETHODIMP TreemapGroupUiaProvider::FindItemByProperty(IRawElementProviderSimple* startAfter,
                                                         PROPERTYID propertyId, VARIANT value,
                                                         IRawElementProviderSimple** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || owner_ == nullptr || owner_->TreemapSurface() == nullptr) return kElementNotAvailable;
    if (propertyId != UIA_NamePropertyId && propertyId != UIA_AutomationIdPropertyId) {
        return UIA_E_INVALIDOPERATION;
    }
    TreemapUiaSurface* surface = owner_->TreemapSurface();

    int startIndex = -1;
    if (startAfter != nullptr) {
        // Only cast when the runtime ID proves the foreign element is one of
        // this tree's own tiles; a sibling from another surface is legal
        // input and must degrade to "start from the beginning".
        Microsoft::WRL::ComPtr<IRawElementProviderFragment> fragment;
        if (SUCCEEDED(startAfter->QueryInterface(IID_IRawElementProviderFragment, &fragment))) {
            SAFEARRAY* ids = nullptr;
            bool ours = false;
            if (SUCCEEDED(fragment->GetRuntimeId(&ids)) && ids != nullptr) {
                LONG lower = 0;
                LONG upper = -1;
                if (SUCCEEDED(SafeArrayGetLBound(ids, 1, &lower)) &&
                    SUCCEEDED(SafeArrayGetUBound(ids, 1, &upper)) && upper - lower + 1 >= 2) {
                    int ns = -1;
                    int kind = -1;
                    LONG i0 = lower;
                    LONG i1 = lower + 1;
                    ours = SUCCEEDED(SafeArrayGetElement(ids, &i0, &ns)) &&
                           SUCCEEDED(SafeArrayGetElement(ids, &i1, &kind)) &&
                           ns == kUiaNsTreemap && kind == 1;
                }
                SafeArrayDestroy(ids);
            }
            if (ours) {
                auto* tile = static_cast<TreemapTileUiaProvider*>(fragment.Get());
                if (tile != nullptr && tile->ScopeHash() == surface->TreemapScopeHash()) {
                    startIndex = surface->TreemapTileIndex(tile->IdHash());
                }
            }
        }
    }

    const int count = surface->TreemapTileCount();
    for (int index = startIndex + 1; index < count; ++index) {
        const TreemapUiaItemInfo info = surface->TreemapTileInfo(index);
        bool matches = false;
        if (propertyId == UIA_NamePropertyId) {
            matches = NameMatches(info.name, value);
        } else if (value.vt == VT_BSTR && value.bstrVal != nullptr) {
            matches = TileAutomationId(info.identityHash) ==
                      std::wstring(value.bstrVal, SysStringLen(value.bstrVal));
        }
        if (matches) {
            auto tile = owner_->TreemapTileElement(info.scopeHash, info.identityHash);
            if (tile != nullptr) {
                Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple;
                if (SUCCEEDED(tile.As(&simple))) {
                    *retVal = simple.Detach();
                }
                return S_OK;
            }
        }
    }
    return S_OK; // not found
}

// ---------------------------------------------------------------------------
// TreemapTileUiaProvider
// ---------------------------------------------------------------------------

TreemapTileUiaProvider::TreemapTileUiaProvider(ColumnViewUiaProvider* owner, uint64_t scopeHash, uint64_t idHash)
    : UiaProviderBase(owner != nullptr ? owner->State() : nullptr),
      owner_(owner), scopeHash_(scopeHash), idHash_(idHash) {}

bool TreemapTileUiaProvider::Resolve(TreemapUiaItemInfo& info) const {
    if (!Alive() || owner_ == nullptr || owner_->TreemapSurface() == nullptr) return false;
    TreemapUiaSurface* surface = owner_->TreemapSurface();
    if (!surface->TreemapFindTile(idHash_, info)) return false;
    // The runtime ID embeds the scope; a provider minted for an old scope can
    // never retarget a tile of the new scope even if the identity hash
    // collided across scopes.
    return info.scopeHash == scopeHash_;
}

STDMETHODIMP TreemapTileUiaProvider::GetPatternProvider(PATTERNID patternId, IUnknown** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    TreemapUiaItemInfo info;
    if (!Resolve(info)) return kElementNotAvailable;
    if (patternId == UIA_InvokePatternId) {
        // Only real navigable tiles invoke; the synthetic aggregate is not
        // one filesystem object and must never navigate to an arbitrary
        // child (semantic-treemap-uia 20.x).
        if (info.invokable) {
            return QueryInterface(IID_IInvokeProvider, reinterpret_cast<void**>(retVal));
        }
    }
    return S_OK;
}

STDMETHODIMP TreemapTileUiaProvider::GetPropertyValue(PROPERTYID propertyId, VARIANT* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    VariantInit(retVal);
    TreemapUiaItemInfo info;
    if (!Resolve(info)) return kElementNotAvailable;
    switch (propertyId) {
        case UIA_ControlTypePropertyId:
            UiaProviderBase::SetVariantInt(retVal, UIA_DataItemControlTypeId);
            return S_OK;
        case UIA_AutomationIdPropertyId:
            UiaProviderBase::SetVariantString(retVal, TileAutomationId(info.identityHash));
            return S_OK;
        case UIA_NamePropertyId:
            UiaProviderBase::SetVariantString(retVal, info.name);
            return S_OK;
        case UIA_HelpTextPropertyId:
            // The full path is the canonical HelpText; the synthetic
            // aggregate has no filesystem object, so its HelpText states
            // what it is.
            UiaProviderBase::SetVariantString(retVal,
                info.synthetic ? L"Other small items — aggregate of tiny siblings" : info.fullPath);
            return S_OK;
        case UIA_FullDescriptionPropertyId:
            UiaProviderBase::SetVariantString(retVal, info.fullDescription);
            return S_OK;
        case UIA_ItemStatusPropertyId:
            UiaProviderBase::SetVariantString(retVal, info.itemStatus);
            return S_OK;
        case UIA_ItemTypePropertyId:
            UiaProviderBase::SetVariantString(retVal, info.itemType);
            return S_OK;
        case UIA_IsEnabledPropertyId:
            UiaProviderBase::SetVariantBool(retVal, info.invokable);
            return S_OK;
        case UIA_IsKeyboardFocusablePropertyId:
            UiaProviderBase::SetVariantBool(retVal, false);
            return S_OK;
        case UIA_IsControlElementPropertyId:
        case UIA_IsContentElementPropertyId:
            UiaProviderBase::SetVariantBool(retVal, true);
            return S_OK;
        case UIA_ProviderDescriptionPropertyId:
            UiaProviderBase::SetVariantString(retVal, L"FastFiles Storage Treemap tile provider");
            return S_OK;
        case UIA_IsOffscreenPropertyId: {
            if (info.offscreen) {
                UiaProviderBase::SetVariantBool(retVal, true);
                return S_OK;
            }
            const UiaViewGeometry geometry = owner_->Geometry();
            const D2D1_RECT_F rectDip = TileRectDip(info, geometry);
            UiaProviderBase::SetVariantBool(retVal,
                UiaRectOffscreen(rectDip, 0.0f, geometry.widthDip, geometry.heightDip));
            return S_OK;
        }
        default:
            break;
    }
    return S_OK;
}

STDMETHODIMP TreemapTileUiaProvider::Navigate(NavigateDirection direction, IRawElementProviderFragment** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || owner_ == nullptr || owner_->TreemapSurface() == nullptr) return kElementNotAvailable;
    TreemapUiaSurface* surface = owner_->TreemapSurface();
    switch (direction) {
        case NavigateDirection_Parent: {
            auto group = owner_->TreemapGroupElement();
            if (group != nullptr) {
                *retVal = group.Detach();
            }
            return S_OK;
        }
        case NavigateDirection_NextSibling:
        case NavigateDirection_PreviousSibling: {
            const int index = surface->TreemapTileIndex(idHash_);
            if (index < 0) return kElementNotAvailable;
            const int target = direction == NavigateDirection_NextSibling ? index + 1 : index - 1;
            if (target < 0 || target >= surface->TreemapTileCount()) return S_OK; // no sibling
            const TreemapUiaItemInfo info = surface->TreemapTileInfo(target);
            auto tile = owner_->TreemapTileElement(info.scopeHash, info.identityHash);
            if (tile != nullptr) {
                *retVal = tile.Detach();
            }
            return S_OK;
        }
        default:
            return S_OK; // no children
    }
}

STDMETHODIMP TreemapTileUiaProvider::GetRuntimeId(SAFEARRAY** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || owner_ == nullptr || owner_->TreemapSurface() == nullptr) return kElementNotAvailable;
    *retVal = MakeIntArray(UiaTreemapTileRuntimeId(scopeHash_, idHash_));
    return S_OK;
}

STDMETHODIMP TreemapTileUiaProvider::get_BoundingRectangle(UiaRect* retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    if (!Alive() || owner_ == nullptr || owner_->TreemapSurface() == nullptr) return kElementNotAvailable;
    TreemapUiaItemInfo info;
    if (!Resolve(info)) return kElementNotAvailable;
    // The exact cached layout the renderer draws and the mouse hit-tester
    // uses: normalized rect -> viewport DIPs -> physical screen pixels via
    // the live shell geometry (origin + DPI + ClientToScreen). No second
    // geometry implementation (semantic-treemap-uia 17.x).
    const UiaViewGeometry geometry = owner_->Geometry();
    const RECT rect = UiaRectToScreen(TileRectDip(info, geometry), geometry);
    retVal->left = static_cast<double>(rect.left);
    retVal->top = static_cast<double>(rect.top);
    retVal->width = static_cast<double>(rect.right - rect.left);
    retVal->height = static_cast<double>(rect.bottom - rect.top);
    return S_OK;
}

STDMETHODIMP TreemapTileUiaProvider::GetEmbeddedFragmentRoots(SAFEARRAY** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    return S_OK;
}

STDMETHODIMP TreemapTileUiaProvider::SetFocus() noexcept {
    // Honest: tiles are not keyboard-focusable (no treemap focus model).
    return S_OK;
}

STDMETHODIMP TreemapTileUiaProvider::get_FragmentRoot(IRawElementProviderFragmentRoot** retVal) noexcept {
    if (retVal == nullptr) return E_POINTER;
    *retVal = nullptr;
    if (!Alive() || owner_ == nullptr) return kElementNotAvailable;
    return owner_->QueryInterface(IID_IRawElementProviderFragmentRoot, reinterpret_cast<void**>(retVal));
}

STDMETHODIMP TreemapTileUiaProvider::Invoke() noexcept {
    if (!Alive() || owner_ == nullptr || owner_->TreemapSurface() == nullptr) return kElementNotAvailable;
    TreemapUiaItemInfo info;
    if (!Resolve(info) || !info.invokable) return kElementNotAvailable;
    // semantic-treemap-uia 11.x: the surface marshals the real navigation
    // mutation to the owning UI thread and re-validates the stable identity
    // there; this worker thread never mutates UI state. Raise the semantic
    // Invoked event for the same element after the action was accepted.
    owner_->TreemapSurface()->TreemapActivateItem(info);
    UiaProviderBase::RaiseInvoked(this);
    return S_OK;
}

} // namespace ffui
