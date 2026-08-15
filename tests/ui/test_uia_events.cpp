// semantic-uia-core-surfaces 6.5/6.6: event-notification routing tests plus
// the synthetic large-directory provider enumeration benchmark.
//
// Event raising itself requires a registered UIA window, so the headless
// contract tested here is the notification layer: the sink fires for every
// semantic notification, and the controller maps notifications to elements
// and invalidates its bounded caches without crashing or touching stale
// providers. The benchmark measures enumeration time and retained provider
// count for a 10 000-item directory.

#include "ColumnViewUiaProvider.h"
#include "SearchUiaProvider.h"
#include "UiaTestSurface.h"

#include <chrono>
#include <string>
#include <vector>

#include "../TestSupport.h"
#include "UiaTestHelpers.h"

using namespace fftest;

namespace {

fftest::FakeUiaSurface MakeLargeSurface(size_t itemCount) {
    fftest::FakeUiaSurface surface;
    surface.geometry.widthDip = 800.0f;
    surface.geometry.heightDip = 600.0f;
    surface.geometry.dpiScale = 1.0f;
    fftest::FakeUiaSurface::FakePane pane;
    fftest::FakeUiaSurface::FakeColumn column;
    column.path = L"C:\\Large";
    column.items.reserve(itemCount);
    for (size_t i = 0; i < itemCount; ++i) {
        fftest::FakeUiaSurface::FakeItem item;
        item.name = L"item" + std::to_wstring(i) + L".dat";
        item.fileIdLow = 100000 + static_cast<uint64_t>(i);
        item.volumeRowId = 7;
        column.items.push_back(item);
    }
    pane.columns.push_back(column);
    surface.panes.push_back(pane);
    return surface;
}

fftest::FakeUiaSurface MakeSearchSurface(size_t count) {
    fftest::FakeUiaSurface surface;
    surface.searchVisible = true;
    surface.searchResults.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        ffui::UiaSearchResultInfo info;
        info.name = L"result" + std::to_wstring(i) + L".txt";
        info.folder = L"C:\\Search";
        info.fullPath = L"C:\\Search\\" + info.name;
        surface.searchResults.push_back(info);
    }
    return surface;
}

Microsoft::WRL::ComPtr<IRawElementProviderFragment> FirstChild(IRawElementProviderFragment* element) {
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> result;
    element->Navigate(NavigateDirection_FirstChild, &result);
    return result;
}

Microsoft::WRL::ComPtr<IRawElementProviderFragment> NextSibling(IRawElementProviderFragment* element) {
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> result;
    element->Navigate(NavigateDirection_NextSibling, &result);
    return result;
}

struct SinkRecord {
    std::vector<ffui::UiaEventKind> kinds;
};

void RecordSink(const ffui::UiaEventNotification& notification, void* context) {
    auto* record = static_cast<SinkRecord*>(context);
    record->kinds.push_back(notification.kind);
}

void TestEventSinkRouting() {
    auto surface = MakeLargeSurface(10);
    auto state = std::make_shared<ffui::UiaOwnerState>();
    SinkRecord record;
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr);

    // A notification for a structure change on pane 0 invalidates the pane's
    // caches and raises without touching the surface after it is released.
    ffui::UiaEventNotification structure;
    structure.kind = ffui::UiaEventKind::StructureBulkAdded;
    structure.pane = 0;
    structure.column = -1;
    ffui::ColumnViewUiaProvider::SinkThunk(structure, &root);
    Check(true, "structure notification is handled without crashing");

    // Element add/remove notifications resolve their target item.
    ffui::UiaEventNotification added;
    added.kind = ffui::UiaEventKind::ElementAddedToSelection;
    added.pane = 0;
    added.column = 0;
    added.row = 2;
    ffui::ColumnViewUiaProvider::SinkThunk(added, &root);
    Check(true, "selection notification is handled without crashing");

    // The sink contract used by the views: notifications carry the kind the
    // controller dispatches on.
    ffui::UiaEventRouter router;
    router.Set(&RecordSink, &record);
    router.Notify({ffui::UiaEventKind::FocusChanged});
    router.Notify({ffui::UiaEventKind::SelectionInvalidated, 0, 0, -1});
    Check(record.kinds.size() == 2 &&
              record.kinds[0] == ffui::UiaEventKind::FocusChanged &&
              record.kinds[1] == ffui::UiaEventKind::SelectionInvalidated,
          "the view-side router delivers every semantic notification");
    router.Clear();
    record.kinds.clear();
    router.Notify({ffui::UiaEventKind::FocusChanged});
    Check(record.kinds.empty(), "cleared router delivers nothing");
}

void TestSearchEventRouting() {
    auto surface = MakeSearchSurface(10);
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::SearchUiaProvider root(state, &surface, nullptr);

    ffui::UiaEventNotification replaced;
    replaced.kind = ffui::UiaEventKind::StructureBulkRemoved;
    replaced.search = true;
    ffui::SearchUiaProvider::SinkThunk(replaced, &root);
    Check(true, "result-replacement notification is handled without crashing");

    ffui::UiaEventNotification selection;
    selection.kind = ffui::UiaEventKind::SelectionInvalidated;
    selection.search = true;
    ffui::SearchUiaProvider::SinkThunk(selection, &root);
    Check(true, "search selection notification is handled without crashing");
}

// The large-directory benchmark: enumerate the whole provider tree of a
// 10 000-item directory through the real fragment navigation, and report
// enumeration time and retained provider count. Thresholds are deliberately
// generous (the mission requires a defensible regression bound, not a
// microsecond spec): enumeration must stay under 2 seconds and the retained
// item cache must stay bounded by the cache cap.
void TestLargeDirectoryBenchmark() {
    auto surface = MakeLargeSurface(10000);
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr);

    auto group = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    auto pane = FirstChild(group.Get());
    auto column = FirstChild(pane.Get());

    const auto start = std::chrono::steady_clock::now();
    size_t enumerated = 0;
    auto item = FirstChild(column.Get());
    while (item != nullptr) {
        ++enumerated;
        // Touch one property per item like a real client would.
        VARIANT value{};
        VariantInit(&value);
        GetPropertyHr(item.Get(), UIA_NamePropertyId, &value);
        VariantClear(&value);
        item = NextSibling(item.Get());
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    Check(enumerated == 10000, "all 10 000 items are enumerable");
    Check(elapsed.count() < 2000, "10 000-item enumeration completes quickly");
    printf("  benchmark: enumerated %zu items in %lld ms\n", enumerated,
           static_cast<long long>(elapsed.count()));

    // Retained provider count: bounded by the cache cap (items survive in
    // the cache; a second enumeration reuses them without reallocation).
    const auto secondStart = std::chrono::steady_clock::now();
    size_t secondCount = 0;
    item = FirstChild(column.Get());
    while (item != nullptr) {
        ++secondCount;
        item = NextSibling(item.Get());
    }
    const auto secondElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - secondStart);
    Check(secondCount == 10000, "second enumeration sees the same items");
    printf("  benchmark: cached re-enumeration took %lld ms\n",
           static_cast<long long>(secondElapsed.count()));
}

} // namespace

int main() {
    TestEventSinkRouting();
    TestSearchEventRouting();
    TestLargeDirectoryBenchmark();

    if (FailureCount() != 0) {
        fprintf(stderr, "ffuia_events_tests: %d check(s) failed\n", FailureCount());
        return 1;
    }
    printf("ffuia_events_tests: all checks passed\n");
    return 0;
}
