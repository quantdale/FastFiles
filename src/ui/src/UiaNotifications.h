#pragma once
// semantic-uia-core-surfaces 1.1/2.6: UIA event notifications.
//
// The views (ColumnView / SearchPanel) push semantic change notifications
// through a sink AFTER the underlying mutation released its locks, so a UIA
// callback that re-enters the provider can never deadlock (design D6/D12).
// The provider controller maps each notification to the affected element(s)
// and raises the standard UIA events; notifications never fire on paint.

#include <cstddef>
#include <cstdint>

namespace ffui {

enum class UiaEventKind {
    FocusChanged,
    SelectionInvalidated,     // owning column / search list
    ElementAddedToSelection,  // target item
    ElementRemovedFromSelection,
    StructureChanged,         // target element's children changed
    StructureBulkRemoved,
    StructureBulkAdded,
    Invoked,
    PropertyChanged,          // IsEnabled / Name
};

struct UiaEventNotification {
    UiaEventKind kind = UiaEventKind::FocusChanged;
    // Navigation-surface target: pane/column/row.
    int pane = -1;
    int column = -1;
    int row = -1;
    // Home-surface target: section kind + item global index. `home` targets
    // the pane's Home group; `group` targets the Column View group element.
    int homeSection = -1;
    int homeIndex = -1;
    bool home = false;
    bool group = false;
    // Search-surface target.
    bool search = false;
    size_t searchIndex = 0;
    // PropertyChanged payload (bool-valued properties: IsEnabled).
    int propertyId = 0;
    bool propertyOldValue = false;
    bool propertyNewValue = false;
};

using UiaEventSink = void (*)(const UiaEventNotification&, void* context);

// RAII-ish holder for a sink + context. The views own one; the provider
// controller registers itself. Null-safe.
class UiaEventRouter {
public:
    void Set(UiaEventSink sink, void* context) {
        sink_ = sink;
        context_ = context;
    }
    bool HasSink() const { return sink_ != nullptr; }
    void Notify(const UiaEventNotification& notification) const {
        if (sink_ != nullptr) {
            sink_(notification, context_);
        }
    }
    void Clear() {
        sink_ = nullptr;
        context_ = nullptr;
    }

private:
    UiaEventSink sink_ = nullptr;
    void* context_ = nullptr;
};

} // namespace ffui
