#pragma once
// semantic-uia-core-surfaces 1.1: shared COM/UIA provider foundation.
//
// Centralizes reference counting, QueryInterface plumbing, VARIANT/property
// helpers, runtime-ID building, event raising, and the owner-lifetime token
// every provider uses. The concrete provider classes stay small: they only
// add their fragment/pattern semantics and identity.

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include <windows.h>
#include <ole2.h>          // IUnknown / MIDL_INTERFACE (WIN32_LEAN_AND_MEAN skips these)
#include <oaidl.h>         // VARIANT
#include <uiautomationcore.h>
#include <uiautomationcoreapi.h> // UiaRaise* event APIs
// SDK 26100 declares the UIA_* property/event ID constants in the client
// header; C4471 (unscoped enum forward declaration) fires under /W4 /WX.
#pragma warning(push)
#pragma warning(disable : 4471)
#include <UIAutomationClient.h>
#pragma warning(pop)

namespace ffui {

// Shared owner-lifetime token (design D6): the surface that owns the
// provider tree (ColumnView / SearchPanel) creates one token alongside the
// tree and flips `alive` to false in its destructor BEFORE member
// destruction. Providers hold a shared_ptr to the token plus a raw surface
// pointer and never dereference the surface when the token is dead, so a
// UIA client that keeps an element past window destruction gets clean
// failures instead of use-after-free.
struct UiaOwnerState {
    std::atomic<bool> alive{true};
    UiaOwnerState() = default;
    UiaOwnerState(const UiaOwnerState&) = delete;
    UiaOwnerState& operator=(const UiaOwnerState&) = delete;
};
using UiaOwnerStatePtr = std::shared_ptr<UiaOwnerState>;

// Emit a concrete QueryInterface implementation. `type_` is the interface
// to expose; AddRef always runs through the canonical
// IRawElementProviderSimple subobject (the first base of UiaProviderBase),
// so refcounts are shared across all interfaces of one object.
#define FFUIA_QI_BEGIN()                                                       \
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) noexcept override {  \
        if (ppv == nullptr) return E_POINTER;                                 \
        *ppv = nullptr;                                                       \
        if (riid == IID_IUnknown || riid == IID_IRawElementProviderSimple) {  \
            *ppv = static_cast<IRawElementProviderSimple*>(this);             \
        } else

#define FFUIA_QI_ENTRY(iid_, type_)                                            \
        if (riid == (iid_)) {                                                  \
            *ppv = static_cast<type_*>(this);                                  \
        } else

#define FFUIA_QI_END()                                                         \
        { return E_NOINTERFACE; }                                              \
        static_cast<IRawElementProviderSimple*>(this)->AddRef();               \
        return S_OK;                                                           \
    }

// Concrete providers derive from UiaProviderBase AND fragment/pattern
// interfaces, which each carry their own IUnknown subobject. The
// most-derived class must therefore override AddRef/Release so both vtables
// share one refcount; UiaProviderBase supplies the counter and helpers.
#define FFUIA_REFCOUNT()                                                       \
    STDMETHODIMP_(ULONG) AddRef() noexcept override {                          \
        return refCount_.fetch_add(1, std::memory_order_relaxed) + 1;          \
    }                                                                          \
    STDMETHODIMP_(ULONG) Release() noexcept override {                         \
        const ULONG remaining = refCount_.fetch_sub(1, std::memory_order_acq_rel) - 1; \
        if (remaining == 0) {                                                  \
            delete this;                                                       \
        }                                                                      \
        return remaining;                                                      \
    }

// Base for every provider: IUnknown + IRawElementProviderSimple with
// provider-agnostic defaults. Derived classes implement QueryInterface (via
// FFUIA_QI_*), the fragment methods, and their patterns.
class UiaProviderBase : public IRawElementProviderSimple {
public:
    explicit UiaProviderBase(UiaOwnerStatePtr state);
    virtual ~UiaProviderBase();

    // IRawElementProviderSimple
    STDMETHODIMP GetPatternProvider(PATTERNID patternId, IUnknown** retVal) noexcept override;
    STDMETHODIMP GetPropertyValue(PROPERTYID propertyId, VARIANT* retVal) noexcept override;
    STDMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple** retVal) noexcept override;
    STDMETHODIMP get_ProviderOptions(ProviderOptions* retVal) noexcept override;

    // True when the owning surface is still alive and safe to dereference.
    bool Alive() const noexcept {
        return state_ && state_->alive;
    }

    // The shared owner-lifetime token (children pass it to their own base).
    UiaOwnerStatePtr State() const noexcept { return state_; }

    // VARIANT helpers (VariantInit'd on entry, VariantClear'd on exit).
    static void SetVariantEmpty(VARIANT* value);
    static void SetVariantInt(VARIANT* value, int32_t integer);
    static void SetVariantBool(VARIANT* value, bool boolean);
    static void SetVariantDouble(VARIANT* value, double number);
    static void SetVariantString(VARIANT* value, const std::wstring& text);
    static void SetVariantRuntimeId(VARIANT* value, const std::vector<int>& runtimeId);

    // UIA event raising wrappers (UIAutomationCore no-ops when no client is
    // attached). All callers are UI-thread mutation sites.
    static HRESULT RaiseFocusChanged(IRawElementProviderSimple* element);
    static HRESULT RaiseSelectionInvalidated(IRawElementProviderSimple* element);
    static HRESULT RaiseElementAddedToSelection(IRawElementProviderSimple* element);
    static HRESULT RaiseElementRemovedFromSelection(IRawElementProviderSimple* element);
    static HRESULT RaiseInvoked(IRawElementProviderSimple* element);
    static HRESULT RaiseStructureChanged(IRawElementProviderSimple* element,
                                         StructureChangeType changeType,
                                         const std::vector<int>* runtimeIds);
    static HRESULT RaisePropertyChanged(IRawElementProviderSimple* element, PROPERTYID propertyId,
                                        const VARIANT& oldValue, const VARIANT& newValue);

    // Real keyboard-focus ownership of the host window.
    static bool HostHasKeyboardFocus(HWND hwnd) {
        return hwnd != nullptr && GetFocus() == hwnd;
    }

protected:
    UiaOwnerStatePtr state_;
    std::atomic<ULONG> refCount_{1};
};

} // namespace ffui
