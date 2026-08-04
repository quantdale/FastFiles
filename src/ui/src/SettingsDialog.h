#pragma once

#include <functional>
#include <map>
#include <optional>
#include <vector>
#include <windows.h>

#include "ffprotocol/Settings.h"
#include "ffprotocol/UiProtocol.h"
#include "NavigationWorkspace.h"

namespace ffui {

class EngineClient;

struct VolumeToggle {
    std::wstring path;
    std::wstring displayName;
    bool indexed = false;     // checkbox state (as persisted via SaveCurrentPage)
    bool inSelection = false; // true when the persisted volume list has an entry for this drive
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

    // settings-and-appearance §7.3: the engine client used to fetch the
    // per-volume index-health report for the Indexing page's status
    // column and explanation text.
    void SetEngineClient(EngineClient* client) { engineClient_ = client; }
    // Privileged-path connection state (from the engine's status badge
    // stream); folded into the per-volume derivation the same way the
    // headline badge does it, so status display and badge never diverge.
    void SetEngineActive(bool active);
    // settings-and-appearance §5.4: dark/light theme for the dialog. Called by
    // WindowShell::ApplyTheme (which also publishes gUiDarkTheme); stores the
    // flag, re-creates the themed background brush, re-applies the DWM dark
    // title bar, and repaints the dialog + its controls.
    void SetDarkTheme(bool dark);

private:
    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    // §5.4: rebuilds themeBrush_ from the active theme's background token.
    void UpdateThemeBrush();
    // §5.4: applies (or removes) the Win11 immersive dark title bar on dialog_.
    void ApplyDwmDarkTitleBar();
    // §5.4: shared WM_CTLCOLOR* helper — sets DC text/bk colors from the active
    // theme and returns the themed background brush for the control/dialog.
    HBRUSH ThemeControlColor(HDC hdc);

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
    // §7.3: requests the engine's per-volume condition report and
    // re-renders the Indexing page's status column + explanation text.
    void RefreshVolumeStatuses();
    void ApplyVolumeStatuses(const std::vector<ffprotocol::VolumeStatusRecord>& records);
    std::wstring StatusTextForVolume(size_t volumeIndex) const;
    void UpdateStatusExplanation(size_t volumeIndex);
    // §9.1: sends the control-plane pause/resume request (global or
    // per-volume, for the currently selected volume) and re-reads status.
    void ToggleSelectedVolumePause();
    void ToggleGlobalPause(bool paused);
    // §9.3: persists the selected pending volume into the volume selection
    // and notifies the engine, without an application restart.
    void AddPendingVolumeToIndexing();

    HWND owner_ = nullptr;
    HWND dialog_ = nullptr;
    HWND tabControl_ = nullptr;
    ffprotocol::Settings* settings_ = nullptr;
    NavigationWorkspace* navigationWorkspace_ = nullptr;
    EngineClient* engineClient_ = nullptr;
    std::function<void()> onChanged_;
    bool visible_ = false;
    bool engineActive_ = false;
    int currentPage_ = 0;
    // §5.4: active theme flag (mirrors gUiDarkTheme, which WindowShell owns)
    // and the dialog background brush created from GetUiTheme(darkTheme_).back-
    // ground; returned from WM_CTLCOLORDLG/WM_CTLCOLOR* so statics/edits/list-
    // boxes are never blinding-white in dark mode. Freed in ~SettingsDialog.
    bool darkTheme_ = false;
    HBRUSH themeBrush_ = nullptr;

    // Search page controls
    HWND searchScopeCombo_ = nullptr;
    HWND retainHistoryCheck_ = nullptr;

    // Navigation page controls
    HWND startupPathEdit_ = nullptr;
    HWND restoreSessionCheck_ = nullptr;

    // Indexing page controls
    HWND volumeList_ = nullptr;
    HWND statusDetail_ = nullptr; // §7.3: headline + conditions explanation text
    HWND addRuleButton_ = nullptr;
    HWND removeRuleButton_ = nullptr;
    HWND moveUpButton_ = nullptr;
    HWND moveDownButton_ = nullptr;
    HWND pauseAllCheck_ = nullptr;   // §9.1: global pause/resume
    HWND pauseResumeButton_ = nullptr; // §9.1: per-volume pause/resume
    HWND addToIndexingButton_ = nullptr; // §9.3: add pending volume to the selection
    std::vector<VolumeToggle> volumes_;
    // drive letter ('C', ...) -> engine-reported condition flags
    std::map<uint8_t, uint8_t> volumeStatusFlags_;
    // §9.1: last global pause request sent to the engine (transient, not
    // persisted); drives the checkbox state across page re-population.
    bool globalPaused_ = false;

    // Storage page controls
    HWND categoryList_ = nullptr;
    HWND addCategoryButton_ = nullptr;
    HWND removeCategoryButton_ = nullptr;

    // Shortcuts page controls
    HWND shortcutList_ = nullptr;
    HWND resetShortcutsButton_ = nullptr;
};

} // namespace ffui
