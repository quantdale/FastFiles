#pragma once

#include <functional>
#include <map>
#include <optional>
#include <vector>
#include <windows.h>

#include "ffprotocol/Settings.h"
#include "NavigationWorkspace.h"

namespace ffui {

struct VolumeToggle {
    std::wstring path;
    std::wstring displayName;
    bool indexed = true;
};

class SettingsDialog {
public:
    SettingsDialog() = default;
    ~SettingsDialog();

    bool Initialize(HWND owner, ffprotocol::Settings* settings,
                    NavigationWorkspace* navigationWorkspace,
                    std::function<void()> onChanged);
    void Show();
    void Hide();
    bool Visible() const { return visible_; }
    bool HandleMessage(MSG& msg);

private:
    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    void CreateControls(HWND hwnd);
    void PopulateGeneralPage();
    void PopulateSearchPage();
    void PopulateNavigationPage();
    void PopulateIndexingPage();
    void PopulateStoragePage();
    void PopulateShortcutsPage();
    void SaveCurrentPage();
    void SwitchPage(int pageIndex);
    void UpdateVolumeList();
    void AddVolumeRule();
    void RemoveVolumeRule();
    void MoveVolumeRuleUp();
    void MoveVolumeRuleDown();
    void ResetShortcuts();
    void CenterWindow(HWND child, HWND parent);

    HWND owner_ = nullptr;
    HWND dialog_ = nullptr;
    HWND tabControl_ = nullptr;
    ffprotocol::Settings* settings_ = nullptr;
    NavigationWorkspace* navigationWorkspace_ = nullptr;
    std::function<void()> onChanged_;
    bool visible_ = false;
    int currentPage_ = 0;

    // Search page controls
    HWND searchScopeCombo_ = nullptr;
    HWND retainHistoryCheck_ = nullptr;

    // Navigation page controls
    HWND startupPathEdit_ = nullptr;
    HWND restoreSessionCheck_ = nullptr;

    // Indexing page controls
    HWND volumeList_ = nullptr;
    HWND addRuleButton_ = nullptr;
    HWND removeRuleButton_ = nullptr;
    HWND moveUpButton_ = nullptr;
    HWND moveDownButton_ = nullptr;
    std::vector<VolumeToggle> volumes_;

    // Storage page controls
    HWND categoryList_ = nullptr;
    HWND addCategoryButton_ = nullptr;
    HWND removeCategoryButton_ = nullptr;

    // Shortcuts page controls
    HWND shortcutList_ = nullptr;
    HWND resetShortcutsButton_ = nullptr;
};

} // namespace ffui
