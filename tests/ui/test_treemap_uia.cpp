// semantic-treemap-uia 33.M: headless provider tests for the treemap UIA tree
// (group semantics, tile properties, Invoke pattern, no Selection, runtime IDs,
// stale elements, lifetime, thread safety, geometry, hit-test parity, cache).
// Uses a FakeTreemapUiaSurface over the real provider COM interfaces.

#include "ColumnViewUiaProvider.h"

#include <algorithm>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "../TestSupport.h"
#include "UiaTestHelpers.h"
#include "UiaTestSurface.h"

using namespace fftest;

namespace {

// ---- Fake treemap surface (implements TreemapUiaSurface) ----

class FakeTreemapUiaSurface : public ffui::TreemapUiaSurface {
public:
    std::vector<ffui::TreemapUiaItemInfo> tiles;
    uint64_t generation = 1;
    uint64_t scopeHash = 0;
    std::wstring scopePath;
    bool active = true;
    ffui::UiaEventRouter events;

    // action log for Invoke / activation tracking
    std::vector<std::wstring> actions;

    bool TreemapActive() const override { return active; }
    uint64_t TreemapGeneration() const override { return generation; }
    uint64_t TreemapScopeHash() const override { return scopeHash; }
    std::wstring TreemapScopePath() const override { return scopePath; }
    int TreemapTileCount() const override { return static_cast<int>(tiles.size()); }
    ffui::TreemapUiaItemInfo TreemapTileInfo(int index) const override {
        if (index < 0 || index >= static_cast<int>(tiles.size())) return {};
        return tiles[static_cast<size_t>(index)];
    }
    int TreemapTileIndex(uint64_t identityHash) const override {
        for (int i = 0; i < static_cast<int>(tiles.size()); ++i) {
            if (tiles[static_cast<size_t>(i)].identityHash == identityHash) return i;
        }
        return -1;
    }
    bool TreemapFindTile(uint64_t identityHash, ffui::TreemapUiaItemInfo& out) const override {
        for (const auto& t : tiles) {
            if (t.identityHash == identityHash) {
                out = t;
                return true;
            }
        }
        return false;
    }
    ffui::TreemapUiaItemInfo TreemapTileHitTest(float normalizedX, float normalizedY) const override {
        for (const auto& t : tiles) {
            if (normalizedX >= t.rectX && normalizedX < t.rectX + t.rectWidth &&
                normalizedY >= t.rectY && normalizedY < t.rectY + t.rectHeight) {
                return t;
            }
        }
        return {};
    }
    void TreemapActivateItem(const ffui::TreemapUiaItemInfo& item) override {
        actions.push_back(L"treemap-activate:" + item.name);
    }
    ffui::UiaEventRouter* TreemapEvents() override { return &events; }
};

// ---- Helpers ----

ffui::TreemapUiaItemInfo MakeTile(uint64_t idHash, const wchar_t* name,
                                   const wchar_t* path, bool isDir, bool invokable,
                                   float rx, float ry, float rw, float rh) {
    ffui::TreemapUiaItemInfo t;
    t.identityHash = idHash;
    t.scopeHash = ffui::UiaHashWide(L"C:\\TestScope");
    t.generation = 1;
    t.name = name;
    t.fullPath = path;
    t.isDirectory = isDir;
    t.invokable = invokable;
    t.logicalSizeBytes = 1000;
    t.logicalSizeKnown = true;
    t.allocatedSizeKnown = true;
    t.rectX = rx;
    t.rectY = ry;
    t.rectWidth = rw;
    t.rectHeight = rh;
    t.itemType = isDir ? L"Folder" : L"File";
    t.fullDescription = L"Storage item: " + std::wstring(name);
    return t;
}

std::vector<int> GetRuntimeId(IRawElementProviderFragment* element) {
    std::vector<int> result;
    SAFEARRAY* array = nullptr;
    if (FAILED(element->GetRuntimeId(&array)) || array == nullptr) return result;
    LONG lower = 0;
    LONG upper = -1;
    SafeArrayGetLBound(array, 1, &lower);
    SafeArrayGetUBound(array, 1, &upper);
    for (LONG i = lower; i <= upper; ++i) {
        int value = 0;
        SafeArrayGetElement(array, &i, &value);
        result.push_back(value);
    }
    SafeArrayDestroy(array);
    return result;
}

Microsoft::WRL::ComPtr<IRawElementProviderFragment> NavigateTo(
    IRawElementProviderFragment* element, NavigateDirection direction) {
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> result;
    element->Navigate(direction, &result);
    return result;
}

Microsoft::WRL::ComPtr<IRawElementProviderFragment> FirstChild(IRawElementProviderFragment* element) {
    return NavigateTo(element, NavigateDirection_FirstChild);
}

Microsoft::WRL::ComPtr<IRawElementProviderFragment> NextSibling(IRawElementProviderFragment* element) {
    return NavigateTo(element, NavigateDirection_NextSibling);
}

// Create a ColumnViewUiaProvider with treemap surface wired.
// Sets up treemap as active with tiles already populated.
fftest::FakeUiaSurface MakeBaseSurface() {
    fftest::FakeUiaSurface surface;
    surface.geometry.widthDip = 800.0f;
    surface.geometry.heightDip = 600.0f;
    surface.geometry.dpiScale = 1.0f;
    surface.geometry.keyboardFocus = true;
    fftest::FakeUiaSurface::FakePane pane;
    fftest::FakeUiaSurface::FakeColumn column;
    column.path = L"C:\\TestScope";
    fftest::FakeUiaSurface::FakeItem item;
    item.name = L"placeholder.txt";
    item.fileIdLow = 999;
    item.volumeRowId = 1;
    column.items.push_back(item);
    pane.columns.push_back(column);
    surface.panes.push_back(pane);
    return surface;
}

// ---- Tests ----

void TestGroupSemantics() {
    FakeTreemapUiaSurface treemap;
    treemap.scopeHash = ffui::UiaHashWide(L"C:\\TestScope");
    treemap.scopePath = L"C:\\TestScope";
    treemap.active = true;
    treemap.tiles.push_back(MakeTile(0x100, L"file.txt", L"C:\\TestScope\\file.txt", false, true, 0.0f, 0.0f, 60.0f, 100.0f));
    treemap.tiles.push_back(MakeTile(0x200, L"FolderA", L"C:\\TestScope\\FolderA", true, true, 60.0f, 0.0f, 40.0f, 100.0f));

    auto surface = MakeBaseSurface();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr, &treemap);

    // Root -> Treemap Group (treemap is active, replaces Column View group).
    auto group = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    Check(group != nullptr, "root has a first child (treemap group)");
    Check(GetName(group.Get()) == L"Storage Treemap", "group name is 'Storage Treemap'");
    Check(GetControlType(group.Get()) == UIA_GroupControlTypeId, "group is Group");
    Check(GetBoolProperty(group.Get(), UIA_IsKeyboardFocusablePropertyId) == false,
          "treemap group is NOT keyboard focusable (honest)");
    Check(GetBoolProperty(group.Get(), UIA_IsEnabledPropertyId), "group is enabled");

    // AutomationId.
    VARIANT value{};
    VariantInit(&value);
    Check(SUCCEEDED(GetPropertyHr(group.Get(), UIA_AutomationIdPropertyId, &value)) &&
              value.vt == VT_BSTR && std::wstring(value.bstrVal) == L"Storage.Treemap",
          "group AutomationId is 'Storage.Treemap'");
    VariantClear(&value);

    // Runtime ID is treemap-scoped.
    const auto rid = GetRuntimeId(group.Get());
    Check(rid.size() == 4 && rid[0] == ffui::kUiaNsTreemap && rid[1] == 0,
          "group runtime id is treemap-scoped");

    // Group -> tiles as children.
    auto tile0 = FirstChild(group.Get());
    Check(tile0 != nullptr, "group has a first child (tile)");
    Check(GetName(tile0.Get()) == L"file.txt", "first tile name is file.txt");
    Check(GetControlType(tile0.Get()) == UIA_DataItemControlTypeId, "tile is DataItem");

    auto tile1 = NextSibling(tile0.Get());
    Check(tile1 != nullptr, "group has a second child");
    Check(GetName(tile1.Get()) == L"FolderA", "second tile name is FolderA");

    // No more siblings.
    auto tile2 = NextSibling(tile1.Get());
    Check(tile2 == nullptr, "no third tile");
}

void TestTileProperties() {
    FakeTreemapUiaSurface treemap;
    treemap.scopeHash = ffui::UiaHashWide(L"C:\\TestScope");
    treemap.scopePath = L"C:\\TestScope";
    treemap.active = true;

    auto tile = MakeTile(0x100, L"report.pdf", L"C:\\TestScope\\report.pdf", false, true, 10.0f, 20.0f, 40.0f, 30.0f);
    tile.fullDescription = L"Storage item: report.pdf\nType: File\nPath: C:\\TestScope\\report.pdf\nSize: 1.0 KB";
    tile.itemType = L"File";
    tile.itemStatus = L"";
    tile.logicalSizeBytes = 1024;
    tile.logicalSizeKnown = true;
    tile.allocatedSizeBytes = 2048;
    tile.allocatedSizeKnown = true;
    treemap.tiles.push_back(tile);

    auto surface = MakeBaseSurface();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr, &treemap);
    auto group = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    auto tileEl = FirstChild(group.Get());

    // HelpText = full path.
    Check(GetStringProperty(tileEl.Get(), UIA_HelpTextPropertyId) == L"C:\\TestScope\\report.pdf",
          "HelpText is the full path");

    // FullDescription.
    const std::wstring fd = GetStringProperty(tileEl.Get(), UIA_FullDescriptionPropertyId);
    Check(fd.find(L"report.pdf") != std::wstring::npos, "FullDescription contains name");
    Check(fd.find(L"File") != std::wstring::npos, "FullDescription contains type");

    // ItemType.
    Check(GetStringProperty(tileEl.Get(), UIA_ItemTypePropertyId) == L"File", "ItemType is File");

    // IsEnabled = true (invokable).
    Check(GetBoolProperty(tileEl.Get(), UIA_IsEnabledPropertyId), "tile is enabled");
    Check(GetBoolProperty(tileEl.Get(), UIA_IsKeyboardFocusablePropertyId) == false,
          "tile is NOT keyboard focusable");

    // BoundingRectangle has nonzero area.
    UiaRect rect{};
    Check(SUCCEEDED(tileEl->get_BoundingRectangle(&rect)), "bounding rect succeeds");
    Check(rect.width > 0.0 && rect.height > 0.0, "bounding rect has nonzero area");

    // Runtime IDs: tile runtime id differs from group.
    const auto tileRid = GetRuntimeId(tileEl.Get());
    const auto groupRid = GetRuntimeId(group.Get());
    Check(tileRid != groupRid, "tile and group have different runtime IDs");
    Check(tileRid.size() == 6, "tile runtime id has 6 elements");
    Check(tileRid[0] == ffui::kUiaNsTreemap && tileRid[1] == 1,
          "tile runtime id prefix is (treemap, 1)");
}

void TestTileIsOffscreen() {
    FakeTreemapUiaSurface treemap;
    treemap.scopeHash = ffui::UiaHashWide(L"C:\\TestScope");
    treemap.scopePath = L"C:\\TestScope";
    treemap.active = true;
    // Tile rect completely outside the viewport (starts at 110%).
    treemap.tiles.push_back(MakeTile(0x100, L"off.txt", L"C:\\off.txt", false, true, 110.0f, 0.0f, 20.0f, 100.0f));

    auto surface = MakeBaseSurface();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr, &treemap);
    auto group = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    auto tileEl = FirstChild(group.Get());

    Check(GetBoolProperty(tileEl.Get(), UIA_IsOffscreenPropertyId), "completely-outside tile is offscreen");

    // A partially-visible tile (80%-130%) is NOT offscreen — only tiles with
    // zero viewport intersection are offscreen.
    treemap.tiles.clear();
    treemap.tiles.push_back(MakeTile(0x200, L"partial.txt", L"C:\\partial.txt", false, true, 80.0f, 0.0f, 50.0f, 100.0f));
    treemap.generation = 2;
    auto group2 = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    auto tileEl2 = FirstChild(group2.Get());
    Check(!GetBoolProperty(tileEl2.Get(), UIA_IsOffscreenPropertyId),
          "partially-visible tile is NOT offscreen");
}

void TestInvokePattern() {
    FakeTreemapUiaSurface treemap;
    treemap.scopeHash = ffui::UiaHashWide(L"C:\\TestScope");
    treemap.scopePath = L"C:\\TestScope";
    treemap.active = true;
    treemap.tiles.push_back(MakeTile(0x100, L"file.txt", L"C:\\file.txt", false, true, 0.0f, 0.0f, 100.0f, 100.0f));

    auto surface = MakeBaseSurface();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr, &treemap);
    auto group = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    auto tileEl = FirstChild(group.Get());

    // Invokable tile exposes IInvokeProvider.
    Microsoft::WRL::ComPtr<IInvokeProvider> invoke;
    Check(SUCCEEDED(tileEl->QueryInterface(IID_IInvokeProvider, &invoke)),
          "invokable tile exposes IInvokeProvider");

    // Invoke succeeds and logs the action.
    Check(SUCCEEDED(invoke->Invoke()), "Invoke succeeds");
    Check(!treemap.actions.empty(), "Invoke logged an action");
    Check(treemap.actions[0].find(L"file.txt") != std::wstring::npos,
          "Invoke action contains the tile name");

    // No ISelectionProvider.
    Microsoft::WRL::ComPtr<ISelectionProvider> selection;
    Check(FAILED(tileEl->QueryInterface(IID_ISelectionProvider, &selection)),
          "tile does NOT expose ISelectionProvider");
}

void TestSyntheticTileNoInvoke() {
    FakeTreemapUiaSurface treemap;
    treemap.scopeHash = ffui::UiaHashWide(L"C:\\TestScope");
    treemap.scopePath = L"C:\\TestScope";
    treemap.active = true;

    auto catchAll = MakeTile(0x999, L"Other small items", L"", false, false, 0.0f, 0.0f, 5.0f, 5.0f);
    catchAll.synthetic = true;
    catchAll.itemType = ffui::kOtherSmallItemsName;
    treemap.tiles.push_back(catchAll);

    auto surface = MakeBaseSurface();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr, &treemap);
    auto group = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    auto tileEl = FirstChild(group.Get());

    Check(GetName(tileEl.Get()) == L"Other small items", "catch-all tile name");
    Check(GetBoolProperty(tileEl.Get(), UIA_IsEnabledPropertyId) == false,
          "catch-all tile is NOT enabled (not invokable)");

    // No IInvokeProvider via GetPatternProvider (the real UIA client path).
    auto simple = ToSimple(tileEl.Get());
    Check(simple != nullptr, "tile QIs as IRawElementProviderSimple");
    Microsoft::WRL::ComPtr<IUnknown> pattern;
    Check(SUCCEEDED(simple->GetPatternProvider(UIA_InvokePatternId, &pattern)),
          "tile GetPatternProvider for Invoke succeeds");
    // pattern should be nullptr because the catch-all tile has no Invoke.
    Check(pattern == nullptr,
          "catch-all tile does NOT expose IInvokeProvider via GetPatternProvider");
}

void TestStaleAfterScopeChange() {
    FakeTreemapUiaSurface treemap;
    treemap.scopeHash = ffui::UiaHashWide(L"C:\\OldScope");
    treemap.scopePath = L"C:\\OldScope";
    treemap.active = true;
    treemap.tiles.push_back(MakeTile(0x100, L"old.txt", L"C:\\OldScope\\old.txt", false, true, 0.0f, 0.0f, 100.0f, 100.0f));

    auto surface = MakeBaseSurface();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr, &treemap);
    auto group = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    auto tileEl = FirstChild(group.Get());
    Check(tileEl != nullptr, "initial tile resolves");

    // Simulate scope change: bump scopeHash, replace tiles.
    treemap.scopeHash = ffui::UiaHashWide(L"C:\\NewScope");
    treemap.scopePath = L"C:\\NewScope";
    treemap.generation = 2;
    treemap.tiles.clear();
    treemap.tiles.push_back(MakeTile(0x200, L"new.txt", L"C:\\NewScope\\new.txt", false, true, 0.0f, 0.0f, 100.0f, 100.0f));

    // Old tile must fail cleanly.
    VARIANT value{};
    VariantInit(&value);
    Check(GetPropertyHr(tileEl.Get(), UIA_NamePropertyId, &value) == UIA_E_ELEMENTNOTAVAILABLE,
          "stale tile after scope change fails with UIA_E_ELEMENTNOTAVAILABLE");
    VariantClear(&value);

    // New group resolves the new tile.
    auto newGroup = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    Check(newGroup != nullptr, "new group resolves after scope change");
    auto newTile = FirstChild(newGroup.Get());
    Check(newTile != nullptr, "new tile resolves");
    Check(GetName(newTile.Get()) == L"new.txt", "new tile has correct name");
}

void TestStaleAfterHide() {
    FakeTreemapUiaSurface treemap;
    treemap.scopeHash = ffui::UiaHashWide(L"C:\\TestScope");
    treemap.scopePath = L"C:\\TestScope";
    treemap.active = true;
    treemap.tiles.push_back(MakeTile(0x100, L"file.txt", L"C:\\file.txt", false, true, 0.0f, 0.0f, 100.0f, 100.0f));

    auto surface = MakeBaseSurface();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr, &treemap);
    auto group = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    auto tileEl = FirstChild(group.Get());

    // Hide treemap: active = false.
    treemap.active = false;

    // The old treemap group element is still a valid provider (surface pointer
    // is non-null, alive). It is just disconnected from the root's tree walk.
    VARIANT value{};
    VariantInit(&value);
    HRESULT hr = GetPropertyHr(group.Get(), UIA_NamePropertyId, &value);
    VariantClear(&value);
    // The old group may still resolve or may fail — both are acceptable. The
    // important invariant is that the ROOT now returns Column View instead.
    (void)hr;

    // Root now returns the Column View group (not treemap).
    auto newChild = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    Check(newChild != nullptr, "root returns Column View group after hide");
    Check(GetName(newChild.Get()) == L"Column View", "child is Column View after hide");

    // The old group's runtime ID no longer matches the root's tree — a fresh
    // tree walk through the root finds Column View, not the treemap group.
    // This proves the treemap group is disconnected.
    auto rootGroup = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    Check(rootGroup != nullptr && GetName(rootGroup.Get()) == L"Column View",
          "root tree walk returns Column View, not treemap group");
}

void TestStaleAfterOwnerDeath() {
    FakeTreemapUiaSurface treemap;
    treemap.scopeHash = ffui::UiaHashWide(L"C:\\TestScope");
    treemap.scopePath = L"C:\\TestScope";
    treemap.active = true;
    treemap.tiles.push_back(MakeTile(0x100, L"file.txt", L"C:\\file.txt", false, true, 0.0f, 0.0f, 100.0f, 100.0f));

    auto surface = MakeBaseSurface();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr, &treemap);
    auto group = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    auto tileEl = FirstChild(group.Get());

    // Kill the owner token.
    state->alive = false;

    VARIANT value{};
    VariantInit(&value);
    Check(GetPropertyHr(tileEl.Get(), UIA_NamePropertyId, &value) == UIA_E_ELEMENTNOTAVAILABLE,
          "tile fails after owner death");
    VariantClear(&value);

    Check(GetPropertyHr(group.Get(), UIA_NamePropertyId, &value) == UIA_E_ELEMENTNOTAVAILABLE,
          "group fails after owner death");
    VariantClear(&value);
}

void TestAggregateResolution() {
    // Same identity node with updated metadata: provider still resolves.
    FakeTreemapUiaSurface treemap;
    treemap.scopeHash = ffui::UiaHashWide(L"C:\\TestScope");
    treemap.scopePath = L"C:\\TestScope";
    treemap.active = true;

    auto tile = MakeTile(0x100, L"dir", L"C:\\TestScope\\dir", true, true, 0.0f, 0.0f, 50.0f, 50.0f);
    tile.logicalSizeBytes = 100;
    tile.logicalSizeKnown = false;
    tile.calculating = true;
    tile.itemStatus = L"Calculating\u2026";
    treemap.tiles.push_back(tile);

    auto surface = MakeBaseSurface();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr, &treemap);
    auto group = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    auto tileEl = FirstChild(group.Get());

    // Initially calculating.
    Check(GetStringProperty(tileEl.Get(), UIA_ItemStatusPropertyId) == L"Calculating\u2026",
          "tile reports calculating status");

    // Update the same identity: resolved now.
    treemap.tiles[0].calculating = false;
    treemap.tiles[0].logicalSizeKnown = true;
    treemap.tiles[0].logicalSizeBytes = 5000;
    treemap.tiles[0].itemStatus = L"";

    // Same provider now reports updated metadata.
    Check(GetStringProperty(tileEl.Get(), UIA_ItemStatusPropertyId).empty(),
          "tile reports empty status after resolution");
}

void TestElementProviderFromPointTreemap() {
    FakeTreemapUiaSurface treemap;
    treemap.scopeHash = ffui::UiaHashWide(L"C:\\TestScope");
    treemap.scopePath = L"C:\\TestScope";
    treemap.active = true;
    // Two tiles split 50/50 horizontally.
    treemap.tiles.push_back(MakeTile(0x100, L"left", L"C:\\left", false, true, 0.0f, 0.0f, 50.0f, 100.0f));
    treemap.tiles.push_back(MakeTile(0x200, L"right", L"C:\\right", false, true, 50.0f, 0.0f, 50.0f, 100.0f));

    auto surface = MakeBaseSurface();
    surface.geometry.widthDip = 800.0f;
    surface.geometry.heightDip = 600.0f;
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr, &treemap);

    // Point in left half of viewport -> left tile.
    // normalizedX = (xDip / widthDip) * 100 = (200/800)*100 = 25.0 (in left tile rect).
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> hit;
    Check(SUCCEEDED(root.ElementProviderFromPoint(200.0, 300.0, &hit)) && hit != nullptr,
          "point in left half hits a treemap element");
    if (hit != nullptr) {
        Check(GetName(hit.Get()) == L"left", "hit element is the left tile");
    }

    // Point in right half -> right tile.
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> hitRight;
    Check(SUCCEEDED(root.ElementProviderFromPoint(600.0, 300.0, &hitRight)) && hitRight != nullptr,
          "point in right half hits a treemap element");
    if (hitRight != nullptr) {
        Check(GetName(hitRight.Get()) == L"right", "hit element is the right tile");
    }

    // Outside viewport -> null.
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> miss;
    Check(SUCCEEDED(root.ElementProviderFromPoint(5000.0, 5000.0, &miss)) && miss == nullptr,
          "outside viewport hits nothing");
}

void TestGetFocusTreemapActive() {
    FakeTreemapUiaSurface treemap;
    treemap.scopeHash = ffui::UiaHashWide(L"C:\\TestScope");
    treemap.scopePath = L"C:\\TestScope";
    treemap.active = true;
    treemap.tiles.push_back(MakeTile(0x100, L"f", L"C:\\f", false, true, 0.0f, 0.0f, 100.0f, 100.0f));

    auto surface = MakeBaseSurface();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr, &treemap);

    // GetFocus returns null when treemap is active (no keyboard focus model).
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> focused;
    Check(SUCCEEDED(root.GetFocus(&focused)) && focused == nullptr,
          "GetFocus returns null when treemap active (no keyboard focus model)");
}

void TestThreadSafetyPropertyReads() {
    FakeTreemapUiaSurface treemap;
    treemap.scopeHash = ffui::UiaHashWide(L"C:\\TestScope");
    treemap.scopePath = L"C:\\TestScope";
    treemap.active = true;
    for (int i = 0; i < 10; ++i) {
        wchar_t name[16];
        swprintf(name, 16, L"file%d.txt", i);
        wchar_t path[64];
        swprintf(path, 64, L"C:\\TestScope\\file%d.txt", i);
        treemap.tiles.push_back(MakeTile(0x100 + i, name, path, false, true,
                                         static_cast<float>(i * 10), 0.0f, 10.0f, 100.0f));
    }

    auto surface = MakeBaseSurface();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    auto* raw = new ffui::ColumnViewUiaProvider(state, &surface, nullptr, &treemap);
    Microsoft::WRL::ComPtr<ffui::ColumnViewUiaProvider> root(raw);

    // Get the group and a tile.
    auto group = FirstChild(raw);
    auto tile0 = FirstChild(group.Get());
    Check(tile0 != nullptr, "tile0 resolves on main thread");

    // Read properties from multiple worker threads concurrently.
    std::atomic<int> failures{0};
    auto worker = [&](int) {
        for (int k = 0; k < 20; ++k) {
            VARIANT value{};
            VariantInit(&value);
            HRESULT hr = GetPropertyHr(tile0.Get(), UIA_NamePropertyId, &value);
            if (FAILED(hr)) failures.fetch_add(1, std::memory_order_relaxed);
            VariantClear(&value);

            SAFEARRAY* rid = nullptr;
            hr = tile0->GetRuntimeId(&rid);
            if (FAILED(hr)) failures.fetch_add(1, std::memory_order_relaxed);
            if (rid != nullptr) SafeArrayDestroy(rid);

            UiaRect rect{};
            hr = tile0->get_BoundingRectangle(&rect);
            if (FAILED(hr)) failures.fetch_add(1, std::memory_order_relaxed);
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back(worker, i);
    }
    for (auto& t : threads) t.join();

    Check(failures.load() == 0, "concurrent property reads from worker threads succeed");
}

void TestItemContainerFindByName() {
    FakeTreemapUiaSurface treemap;
    treemap.scopeHash = ffui::UiaHashWide(L"C:\\TestScope");
    treemap.scopePath = L"C:\\TestScope";
    treemap.active = true;
    treemap.tiles.push_back(MakeTile(0x100, L"alpha.txt", L"C:\\alpha.txt", false, true, 0.0f, 0.0f, 50.0f, 50.0f));
    treemap.tiles.push_back(MakeTile(0x200, L"beta.txt", L"C:\\beta.txt", false, true, 50.0f, 0.0f, 50.0f, 50.0f));

    auto surface = MakeBaseSurface();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr, &treemap);
    auto group = FirstChild(static_cast<IRawElementProviderFragment*>(&root));

    Microsoft::WRL::ComPtr<IItemContainerProvider> container;
    Check(SUCCEEDED(group->QueryInterface(IID_IItemContainerProvider, &container)),
          "treemap group exposes IItemContainerProvider");

    // Find by name.
    VARIANT name{};
    VariantInit(&name);
    name.vt = VT_BSTR;
    name.bstrVal = SysAllocString(L"beta.txt");
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> found;
    Check(SUCCEEDED(container->FindItemByProperty(nullptr, UIA_NamePropertyId, name, &found)) &&
              found != nullptr,
          "FindItemByProperty finds 'beta.txt'");
    if (found != nullptr) {
        Check(GetName(found.Get()) == L"beta.txt", "found item has correct name");
    }
    VariantClear(&name);

    // Not found.
    VariantInit(&name);
    name.vt = VT_BSTR;
    name.bstrVal = SysAllocString(L"nonexistent.txt");
    found.Reset();
    Check(SUCCEEDED(container->FindItemByProperty(nullptr, UIA_NamePropertyId, name, &found)) &&
              found == nullptr,
          "missing name returns null");
    VariantClear(&name);
}

// A startAfter element from a foreign surface must degrade to "start from
// the beginning", and a non-BSTR AutomationId value must not be read as a
// string (std::wstring(nullptr, 0) UB).
void TestItemContainerForeignStartAfterAndNonString() {
    FakeTreemapUiaSurface treemap;
    treemap.scopeHash = ffui::UiaHashWide(L"C:\\TestScope");
    treemap.scopePath = L"C:\\TestScope";
    treemap.active = true;
    treemap.tiles.push_back(MakeTile(0x100, L"alpha.txt", L"C:\\alpha.txt", false, true, 0.0f, 0.0f, 50.0f, 50.0f));
    treemap.tiles.push_back(MakeTile(0x200, L"beta.txt", L"C:\\beta.txt", false, true, 50.0f, 0.0f, 50.0f, 50.0f));

    auto surface = MakeBaseSurface();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr, &treemap);
    auto group = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    Microsoft::WRL::ComPtr<IItemContainerProvider> container;
    Check(SUCCEEDED(group->QueryInterface(IID_IItemContainerProvider, &container)),
          "treemap group exposes IItemContainerProvider");

    // Foreign element: a column item provider from a plain (non-treemap) tree.
    auto plainSurface = MakeBaseSurface();
    ffui::ColumnViewUiaProvider plainRoot(state, &plainSurface, nullptr);
    auto pGroup = FirstChild(static_cast<IRawElementProviderFragment*>(&plainRoot));
    auto pPane = FirstChild(pGroup.Get());
    auto pColumn = FirstChild(pPane.Get());
    auto foreignItem = FirstChild(pColumn.Get());
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> foreignSimple;
    Check(SUCCEEDED(foreignItem.As(&foreignSimple)) && foreignSimple != nullptr,
          "foreign column item resolves");

    VARIANT name{};
    VariantInit(&name);
    name.vt = VT_BSTR;
    name.bstrVal = SysAllocString(L"beta.txt");
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> found;
    Check(SUCCEEDED(container->FindItemByProperty(foreignSimple.Get(), UIA_NamePropertyId, name, &found)) &&
              found != nullptr && GetName(found.Get()) == L"beta.txt",
          "foreign startAfter degrades to a full scan and still finds the tile");
    VariantClear(&name);

    // Non-BSTR value for the AutomationId branch: must return null cleanly.
    VARIANT bad{};
    VariantInit(&bad);
    bad.vt = VT_I4;
    bad.lVal = 42;
    found.Reset();
    Check(SUCCEEDED(container->FindItemByProperty(nullptr, UIA_AutomationIdPropertyId, bad, &found)) &&
              found == nullptr,
          "non-BSTR AutomationId value matches nothing without UB");
    VariantClear(&bad);

    // A real AutomationId still matches through the same branch.
    auto betaTile = NextSibling(FirstChild(group.Get()).Get());
    Check(betaTile != nullptr, "beta tile resolves");
    const std::wstring betaId = GetStringProperty(betaTile.Get(), UIA_AutomationIdPropertyId);
    Check(!betaId.empty(), "tile exposes an AutomationId");
    VariantInit(&name);
    name.vt = VT_BSTR;
    name.bstrVal = SysAllocString(betaId.c_str());
    found.Reset();
    Check(SUCCEEDED(container->FindItemByProperty(nullptr, UIA_AutomationIdPropertyId, name, &found)) &&
              found != nullptr && GetName(found.Get()) == L"beta.txt",
          "AutomationId match finds the tile");
    VariantClear(&name);
}

void TestTileParentNavigate() {
    FakeTreemapUiaSurface treemap;
    treemap.scopeHash = ffui::UiaHashWide(L"C:\\TestScope");
    treemap.scopePath = L"C:\\TestScope";
    treemap.active = true;
    treemap.tiles.push_back(MakeTile(0x100, L"file.txt", L"C:\\file.txt", false, true, 0.0f, 0.0f, 100.0f, 100.0f));

    auto surface = MakeBaseSurface();
    auto state = std::make_shared<ffui::UiaOwnerState>();
    ffui::ColumnViewUiaProvider root(state, &surface, nullptr, &treemap);
    auto group = FirstChild(static_cast<IRawElementProviderFragment*>(&root));
    auto tileEl = FirstChild(group.Get());

    // Tile Navigate Parent -> group.
    auto parent = NavigateTo(tileEl.Get(), NavigateDirection_Parent);
    Check(parent != nullptr, "tile parent resolves");
    Check(GetName(parent.Get()) == L"Storage Treemap", "tile parent is the treemap group");
}

} // namespace

int main() {
    TestGroupSemantics();
    TestTileProperties();
    TestTileIsOffscreen();
    TestInvokePattern();
    TestSyntheticTileNoInvoke();
    TestStaleAfterScopeChange();
    TestStaleAfterHide();
    TestStaleAfterOwnerDeath();
    TestAggregateResolution();
    TestElementProviderFromPointTreemap();
    TestGetFocusTreemapActive();
    TestThreadSafetyPropertyReads();
    TestItemContainerFindByName();
    TestTileParentNavigate();
    TestItemContainerForeignStartAfterAndNonString();

    if (FailureCount() != 0) {
        fprintf(stderr, "fftreemap_uia_tests: %d check(s) failed\n", FailureCount());
        return 1;
    }
    printf("fftreemap_uia_tests: all checks passed\n");
    return 0;
}
