#include "SettingsDialog.h"

#include <algorithm>
#include <commctrl.h>
#include <filesystem>
#include <memory>
#include <shlwapi.h>

#include "EngineClient.h"
#include "UITheme.h"
#include "UiComponents.h"
#include "ffprotocol/IndexHealth.h"

#pragma comment(lib, "shlwapi.lib")

namespace ffui {
namespace {

constexpr UINT WM_APP_VOLUME_STATUS = WM_APP + 21;
constexpr int kDialogWidth = 620;
constexpr int kDialogHeight = 520;
constexpr int kTabControlHeight = 28;
constexpr int kPageTop = 36;
constexpr int kPagePadding = 12;
constexpr int kControlHeight = 24;
constexpr int kControlSpacing = 32;

// Scale a DIP metric to physical pixels for Win32 control layout.
int Scaled(int dipValue) {
    return static_cast<int>(ffui::UiScale(static_cast<float>(dipValue)));
}

constexpr wchar_t const* kPageNames[] = {
    L"General",
    L"Search",
    L"Navigation",
    L"Indexing",
    L"Storage",
    L"Shortcuts"
};

std::wstring GetWindowText(HWND hwnd) {
    int length = GetWindowTextLengthW(hwnd);
    std::wstring result(length, L'\0');
    GetWindowTextW(hwnd, result.data(), length + 1);
    result.resize(length);
    return result;
}

void InsertColumn(HWND list, int index, int width, const wchar_t* text) {
    LVCOLUMNW column{};
    column.mask = LVCF_TEXT | LVCF_WIDTH;
    column.pszText = const_cast<wchar_t*>(text);
    column.cx = width;
    ListView_InsertColumn(list, index, &column);
}

// ui-v3 settings: applies the product body font to every child control so
// labels and values stop rendering in the raw system GUI font.
BOOL CALLBACK SetChildBodyFont(HWND child, LPARAM lParam) {
    SendMessageW(child, WM_SETFONT, lParam, TRUE);
    return TRUE;
}

} // namespace

SettingsDialog::~SettingsDialog() {
    Hide();
    if (themeBrush_) {
        DeleteObject(themeBrush_);
        themeBrush_ = nullptr;
    }
    if (bodyFont_) {
        DeleteObject(bodyFont_);
        bodyFont_ = nullptr;
    }
}

bool SettingsDialog::Initialize(HWND owner, ffprotocol::Settings* settings,
                                const CommandRegistry* commandRegistry, ShortcutMap* shortcuts,
                                NavigationWorkspace* navigationWorkspace,
                                std::function<void()> onChanged) {
    owner_ = owner;
    settings_ = settings;
    if (settings_) settingsDraft_ = *settings_;
    commandRegistry_ = commandRegistry;
    shortcuts_ = shortcuts;
    if (shortcuts_) shortcutDraft_ = *shortcuts_;
    navigationWorkspace_ = navigationWorkspace;
    onChanged_ = std::move(onChanged);

    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProcThunk;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"FastFilesSettingsDialog";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_3DFACE);
    RegisterClassW(&wc);

    dialog_ = CreateWindowExW(WS_EX_DLGMODALFRAME, L"FastFilesSettingsDialog", L"FastFiles Settings",
                              WS_POPUP | WS_CAPTION | WS_SYSMENU,
                              0, 0, Scaled(kDialogWidth), Scaled(kDialogHeight),
                              owner_, nullptr, GetModuleHandleW(nullptr), this);
    if (!dialog_) return false;

    // §5.4: themed dialog background + immersive dark title bar. Native tab,
    // list, combo, and edit chrome pick up Explorer/CFD visual styles so dark
    // mode is not a mix of tokenized background and system-white controls.
    UpdateThemeBrush();
    ApplyDwmDarkTitleBar();

    CenterWindow(dialog_, owner_);

    tabControl_ = CreateWindowExW(0, WC_TABCONTROLW, L"",
                                  WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                  Scaled(kPagePadding), Scaled(kPagePadding), Scaled(kDialogWidth) - 2 * Scaled(kPagePadding), Scaled(kTabControlHeight),
                                  dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);
    for (int i = 0; i < 6; ++i) {
        TCITEMW item{TCIF_TEXT};
        item.pszText = const_cast<wchar_t*>(kPageNames[i]);
        TabCtrl_InsertItem(tabControl_, i, &item);
    }

    CreateControls(dialog_);
    PopulateGeneralPage();
    PopulateSearchPage();
    PopulateNavigationPage();
    PopulateIndexingPage();
    PopulateStoragePage();
    PopulateShortcutsPage();

    // ui-v3 settings: one DPI-scaled Segoe UI body format for all children —
    // labels, edits, combos, buttons, lists — replacing the system default.
    bodyFont_ = CreateFontW(-static_cast<int>(UiScale(ffui::UiMetrics::kFontSizeBody)), 0, 0, 0,
                            FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    if (bodyFont_ != nullptr) {
        EnumChildWindows(dialog_, SetChildBodyFont,
                         reinterpret_cast<LPARAM>(bodyFont_));
    }

    SwitchPage(0);
    ApplyNativeControlThemes();
    // The dialog is created hidden and shown on demand (Settings gear /
    // Ctrl+, / app.settings command). Showing it here made it cover the
    // browser on every launch (recovery-degraded-browsing finding: the
    // unconditional ShowWindow was a leftover from a compile-health fix).
    return true;
}

void SettingsDialog::Show() {
    if (dialog_) {
        if (shortcuts_) shortcutDraft_ = *shortcuts_;
        if (settings_) settingsDraft_ = *settings_;
        // Re-populate the simple pages from the fresh draft so a previous
        // session's Cancelled edits don't linger in the controls.
        PopulateGeneralPage();
        PopulateSearchPage();
        PopulateNavigationPage();
        SwitchPage(tabControl_ ? TabCtrl_GetCurSel(tabControl_) : 0);
        ShowWindow(dialog_, SW_SHOW);
        SetForegroundWindow(dialog_);
        visible_ = true;
        if (volumeList_) {
            UpdateVolumeList();
            RefreshVolumeStatuses();
        }
    }
}

void SettingsDialog::Hide() {
    if (dialog_) {
        ShowWindow(dialog_, SW_HIDE);
        visible_ = false;
    }
}

void SettingsDialog::SetDarkTheme(bool dark) {
    darkTheme_ = dark;
    // The class background stays a stable system brush; the live background is
    // delivered by the WM_CTLCOLORDLG/WM_CTLCOLOR* handlers from themeBrush_.
    UpdateThemeBrush();
    ApplyDwmDarkTitleBar();
    ApplyNativeControlThemes();
    if (dialog_) {
        // Repaint the dialog and all children so the themed brush/text colors
        // (WM_CTLCOLOR*) take effect without recreating any control.
        RedrawWindow(dialog_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    }
}

void SettingsDialog::ApplyNativeControlThemes() {
    const bool dark = darkTheme_;
    ApplyNativeExplorerTheme(tabControl_, dark);
    ApplyNativeExplorerTheme(volumeList_, dark);
    ApplyNativeExplorerTheme(ruleList_, dark);
    ApplyNativeExplorerTheme(categoryList_, dark);
    ApplyNativeExplorerTheme(shortcutList_, dark);
    ApplyNativeExplorerTheme(themeCombo_, dark, NativeExplorerTheme::Combo);
    ApplyNativeExplorerTheme(densityCombo_, dark, NativeExplorerTheme::Combo);
    ApplyNativeExplorerTheme(searchScopeCombo_, dark, NativeExplorerTheme::Combo);
    ApplyNativeExplorerTheme(previewSizeEdit_, dark, NativeExplorerTheme::Combo);
    ApplyNativeExplorerTheme(startupPathEdit_, dark, NativeExplorerTheme::Combo);
    ApplyNativeExplorerTheme(shortcutEdit_, dark, NativeExplorerTheme::Combo);
    ApplyNativeExplorerTheme(categoryEdit_, dark, NativeExplorerTheme::Combo);
    for (HWND list : {volumeList_, ruleList_, categoryList_, shortcutList_}) {
        if (list == nullptr) continue;
        const UiTheme theme = GetUiTheme(dark);
        ListView_SetBkColor(list, ToColorRef(theme.background));
        ListView_SetTextColor(list, ToColorRef(theme.text));
        ListView_SetTextBkColor(list, ToColorRef(theme.background));
    }
}

void SettingsDialog::UpdateThemeBrush() {
    if (themeBrush_) {
        DeleteObject(themeBrush_);
        themeBrush_ = nullptr;
    }
    themeBrush_ = CreateSolidBrush(ToColorRef(GetUiTheme(darkTheme_).background));
}

void SettingsDialog::ApplyDwmDarkTitleBar() {
    if (!dialog_) return;
    // Win11 immersive dark mode for the title bar / window chrome (attribute
    // 20); harmless no-op on systems without the attribute (same pattern as
    // WindowShell::ApplyTheme).
    if (HMODULE dwmapi = GetModuleHandleW(L"dwmapi.dll")) {
        using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
        auto dwmSetAttribute = reinterpret_cast<DwmSetWindowAttributeFn>(GetProcAddress(dwmapi, "DwmSetWindowAttribute"));
        if (dwmSetAttribute) {
            const BOOL useDark = darkTheme_ ? TRUE : FALSE;
            dwmSetAttribute(dialog_, 20, &useDark, sizeof(useDark));  // DWMWA_USE_IMMERSIVE_DARK_MODE (20)
        }
    }
}

HBRUSH SettingsDialog::ThemeControlColor(HDC hdc) {
    const UiTheme theme = GetUiTheme(darkTheme_);
    SetTextColor(hdc, ToColorRef(theme.text));
    SetBkColor(hdc, ToColorRef(theme.background));
    return themeBrush_;
}

bool SettingsDialog::HandleMessage(MSG& msg) {
    if (!visible_ || !dialog_) return false;
    if (msg.hwnd == dialog_ || IsChild(dialog_, msg.hwnd)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        return true;
    }
    return false;
}

LRESULT CALLBACK SettingsDialog::WndProcThunk(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* self = reinterpret_cast<SettingsDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) return self->HandleMessage(hwnd, message, wParam, lParam);
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT SettingsDialog::HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDOK:
                    FinishShortcutEdit(true);
                    FinishCategoryEdit(true);
                    SaveCurrentPage();
                    if (settings_) *settings_ = settingsDraft_;
                    if (onChanged_) onChanged_();
                    Hide();
                    return 0;
                case IDCANCEL:
                    // Cancel discards: the draft (and any in-place editor) is
                    // thrown away; *settings_ is only written on OK.
                    FinishShortcutEdit(false);
                    FinishCategoryEdit(false);
                    Hide();
                    return 0;
                case 40001: // Reset shortcuts button
                    ResetShortcuts();
                    PopulateShortcutsPage();
                    return 0;
            }
            if (HIWORD(wParam) == BN_CLICKED) {
                if (lParam == reinterpret_cast<LPARAM>(addRuleButton_)) {
                    AddVolumeRule();
                    return 0;
                }
                if (lParam == reinterpret_cast<LPARAM>(removeRuleButton_)) {
                    RemoveVolumeRule();
                    return 0;
                }
                if (lParam == reinterpret_cast<LPARAM>(moveUpButton_)) {
                    MoveVolumeRuleUp();
                    return 0;
                }
                if (lParam == reinterpret_cast<LPARAM>(moveDownButton_)) {
                    MoveVolumeRuleDown();
                    return 0;
                }
                if (lParam == reinterpret_cast<LPARAM>(pauseAllCheck_)) {
                    // §9.1: global pause/resume control-plane request.
                    ToggleGlobalPause(SendMessageW(pauseAllCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    return 0;
                }
                if (lParam == reinterpret_cast<LPARAM>(pauseResumeButton_)) {
                    // §9.1: per-volume pause/resume for the selection.
                    ToggleSelectedVolumePause();
                    return 0;
                }
                if (lParam == reinterpret_cast<LPARAM>(addToIndexingButton_)) {
                    // §9.3: persist + notify without an application restart.
                    AddPendingVolumeToIndexing();
                    return 0;
                }
                if (lParam == reinterpret_cast<LPARAM>(addCategoryButton_)) {
                    // Add an empty category row into the draft and start
                    // editing its name immediately; committed on OK.
                    if (settings_) {
                        settingsDraft_.storageCategories.emplace_back(L"New Category", L"");
                        PopulateStoragePage();
                        BeginCategoryEdit(static_cast<int>(settingsDraft_.storageCategories.size()) - 1, 0);
                    }
                    return 0;
                }
                if (lParam == reinterpret_cast<LPARAM>(removeCategoryButton_)) {
                    if (!categoryList_ || !settings_) return 0;
                    const int selected = ListView_GetNextItem(categoryList_, -1, LVNI_SELECTED);
                    if (selected >= 0 && selected < static_cast<int>(settingsDraft_.storageCategories.size())) {
                        settingsDraft_.storageCategories.erase(settingsDraft_.storageCategories.begin() + selected);
                        PopulateStoragePage();
                    }
                    return 0;
                }
            }
            break;
        case WM_CLOSE:
            FinishShortcutEdit(false);
            FinishCategoryEdit(false);
            Hide();
            return 0;
        case WM_APP_VOLUME_STATUS: {
            std::unique_ptr<std::vector<ffprotocol::VolumeStatusRecord>> records(
                reinterpret_cast<std::vector<ffprotocol::VolumeStatusRecord>*>(lParam));
            if (records) {
                ApplyVolumeStatuses(*records);
            }
            return 0;
        }
        case WM_NOTIFY: {
            auto* hdr = reinterpret_cast<NMHDR*>(lParam);
            if (hdr->hwndFrom == shortcutList_ && hdr->code == NM_DBLCLK) {
                const auto* info = reinterpret_cast<NMITEMACTIVATE*>(lParam);
                if (info->iItem >= 0 && info->iSubItem == 1) BeginShortcutEdit(info->iItem);
                return 0;
            }
            if (hdr->hwndFrom == categoryList_ && hdr->code == NM_DBLCLK) {
                const auto* info = reinterpret_cast<NMITEMACTIVATE*>(lParam);
                if (info->iItem >= 0 && (info->iSubItem == 0 || info->iSubItem == 1)) {
                    BeginCategoryEdit(info->iItem, info->iSubItem);
                }
                return 0;
            }
            if (hdr->hwndFrom == tabControl_ && hdr->code == TCN_SELCHANGE) {
                SwitchPage(TabCtrl_GetCurSel(tabControl_));
            }
            if (hdr->hwndFrom == volumeList_ && hdr->code == LVN_ITEMCHANGED) {
                auto* info = reinterpret_cast<NMLISTVIEW*>(lParam);
                if ((info->uChanged & LVIF_STATE) && (info->uNewState & LVIS_STATEIMAGEMASK)) {
                    const int state = (info->uNewState & LVIS_STATEIMAGEMASK) >> 12;
                    if (state == 1 || state == 2) { // unchecked or checked
                        if (info->iItem >= 0 && info->iItem < static_cast<int>(volumes_.size())) {
                            volumes_[info->iItem].indexed = (state == 2);
                            // Checking a previously unselected drive is the
                            // §9.3 "add to indexing" decision (persisted on
                            // OK via SaveCurrentPage); unchecking is the
                            // §9.2 disable decision.
                            volumes_[info->iItem].inSelection = true;
                            UpdateStatusExplanation(static_cast<size_t>(info->iItem));
                        }
                    }
                }
                if ((info->uChanged & LVIF_STATE) && (info->uNewState & LVIS_SELECTED)) {
                    UpdateStatusExplanation(static_cast<size_t>(info->iItem));
                    UpdateRuleList();
                }
            }
            break;
        }
        // §5.4: themed backgrounds/text for the dialog + statics/edits/list-
        // boxes so dark mode never produces blinding-white chrome. Buttons and
        // the tab control stay stock (documented trade-off; the dialog surface,
        // statics, edit fields, and listview/listbox bodies are themed here).
        case WM_ERASEBKGND: {
            RECT client{};
            GetClientRect(hwnd, &client);
            HBRUSH brush = ThemeControlColor(reinterpret_cast<HDC>(wParam));
            if (brush != nullptr) FillRect(reinterpret_cast<HDC>(wParam), &client, brush);
            return 1;
        }
        case WM_CTLCOLORDLG:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            return reinterpret_cast<LRESULT>(ThemeControlColor(reinterpret_cast<HDC>(wParam)));
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void SettingsDialog::CreateControls(HWND hwnd) {
    (void)hwnd;
    // OK/Cancel buttons
    CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                    Scaled(kDialogWidth) - Scaled(180), Scaled(kDialogHeight) - Scaled(36), Scaled(80), Scaled(24),
                    dialog_, reinterpret_cast<HMENU>(IDOK), GetModuleHandleW(nullptr), nullptr);
    CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    Scaled(kDialogWidth) - Scaled(90), Scaled(kDialogHeight) - Scaled(36), Scaled(80), Scaled(24),
                    dialog_, reinterpret_cast<HMENU>(IDCANCEL), GetModuleHandleW(nullptr), nullptr);
}

void SettingsDialog::PopulateGeneralPage() {
    if (!themeCombo_) {
        themeLabel_ = CreateWindowExW(0, L"STATIC", L"Theme:", WS_CHILD | WS_VISIBLE,
                        Scaled(kPagePadding), Scaled(kPagePadding) + Scaled(34), Scaled(90), Scaled(20),
                        dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);
        themeCombo_ = CreateWindowExW(0, L"COMBOBOX", L"",
                                      WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                      Scaled(kPagePadding) + Scaled(100), Scaled(kPagePadding) + Scaled(30), Scaled(200), Scaled(200),
                                      dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(themeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Light"));
        SendMessageW(themeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Dark"));
        SendMessageW(themeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Follow System"));
        densityLabel_ = CreateWindowExW(0, L"STATIC", L"Density:", WS_CHILD | WS_VISIBLE,
                        Scaled(kPagePadding), Scaled(kPagePadding) + Scaled(66), Scaled(90), Scaled(20),
                        dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);
        densityCombo_ = CreateWindowExW(0, L"COMBOBOX", L"",
                                        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                        Scaled(kPagePadding) + Scaled(100), Scaled(kPagePadding) + Scaled(62), Scaled(200), Scaled(200),
                                        dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(densityCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Compact"));
        SendMessageW(densityCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Comfortable"));
        previewSizeLabel_ = CreateWindowExW(0, L"STATIC", L"Automatic preview size limit (bytes):", WS_CHILD | WS_VISIBLE,
                        Scaled(kPagePadding), Scaled(kPagePadding) + Scaled(126), Scaled(240), Scaled(20),
                        dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);
        previewSizeEdit_ = CreateWindowExW(0, L"EDIT", L"",
                                           WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER,
                                           Scaled(kPagePadding) + Scaled(100), Scaled(kPagePadding) + Scaled(152), Scaled(200), Scaled(24),
                                           dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);
    }
    if (!previewCheck_) {
        previewCheck_ = CreateWindowExW(0, L"BUTTON", L"Enable automatic preview",
                                        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                        Scaled(kPagePadding) + Scaled(100), Scaled(kPagePadding) + Scaled(94), Scaled(220), Scaled(20),
                                        dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);
    }

    if (settings_) {
        int themeIndex = 2; // FollowSystem
        if (settingsDraft_.theme == ffprotocol::ThemePreference::Light) themeIndex = 0;
        else if (settingsDraft_.theme == ffprotocol::ThemePreference::Dark) themeIndex = 1;
        SendMessageW(themeCombo_, CB_SETCURSEL, themeIndex, 0);
        SendMessageW(densityCombo_, CB_SETCURSEL,
                     settingsDraft_.density == ffprotocol::DensityPreference::Comfortable ? 1 : 0, 0);
        SendMessageW(previewCheck_, BM_SETCHECK, settingsDraft_.previewEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
        SetWindowTextW(previewSizeEdit_, std::to_wstring(settingsDraft_.maxAutoPreviewBytes).c_str());
    }
}

void SettingsDialog::PopulateSearchPage() {
    if (!searchScopeCombo_) {
        searchScopeCombo_ = CreateWindowExW(0, L"COMBOBOX", L"",
                                             WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                             Scaled(kPagePadding) + Scaled(100), Scaled(kPagePadding) + Scaled(30), Scaled(200), Scaled(200),
                                             dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(searchScopeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Current Folder"));
        SendMessageW(searchScopeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Current Folder + Subfolders"));
        SendMessageW(searchScopeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Current Drive"));
        SendMessageW(searchScopeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"All Indexed Locations"));
        SendMessageW(searchScopeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Everywhere"));
    }
    if (!retainHistoryCheck_) {
        retainHistoryCheck_ = CreateWindowExW(0, L"BUTTON", L"Retain search history",
                                              WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                              Scaled(kPagePadding) + Scaled(100), Scaled(kPagePadding) + Scaled(60), Scaled(200), Scaled(20),
                                              dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);
    }

    // Map the persisted scope explicitly; unknown values fall back to the
    // persisted default "Everywhere" (index 4), never silently to index 0.
    int scopeIndex = 4;
    if (settings_) {
        const std::wstring& scope = settings_->defaultSearchScope;
        if (scope == L"Current Folder") scopeIndex = 0;
        else if (scope == L"Current Folder + Subfolders") scopeIndex = 1;
        else if (scope == L"Current Drive") scopeIndex = 2;
        else if (scope == L"All Indexed Locations") scopeIndex = 3;
        else if (scope == L"Everywhere") scopeIndex = 4;
    }
    SendMessageW(searchScopeCombo_, CB_SETCURSEL, scopeIndex, 0);
    SendMessageW(retainHistoryCheck_, BM_SETCHECK, settings_ && settings_->retainSearchHistory ? BST_CHECKED : BST_UNCHECKED, 0);
}

void SettingsDialog::PopulateNavigationPage() {
    if (!startupPathEdit_) {
        startupLabel_ = CreateWindowExW(0, L"STATIC", L"Startup location:", WS_CHILD | WS_VISIBLE,
                        Scaled(kPagePadding) + Scaled(100), Scaled(kPagePadding) + Scaled(30), Scaled(160), Scaled(20),
                        dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);
        startupPathEdit_ = CreateWindowExW(0, L"EDIT", L"",
                                           WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                           Scaled(kPagePadding) + Scaled(100), Scaled(kPagePadding) + Scaled(55), Scaled(300), Scaled(24),
                                           dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);
    }
    if (!restoreSessionCheck_) {
        restoreSessionCheck_ = CreateWindowExW(0, L"BUTTON", L"Restore previous session on startup",
                                               WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                               Scaled(kPagePadding) + Scaled(100), Scaled(kPagePadding) + Scaled(85), Scaled(250), Scaled(20),
                                               dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);
    }

    if (settings_) {
        SetWindowTextW(startupPathEdit_, settings_->startupLocation.c_str());
        SendMessageW(restoreSessionCheck_, BM_SETCHECK, settings_->restorePreviousSession ? BST_CHECKED : BST_UNCHECKED, 0);
    }
}

void SettingsDialog::SwitchPage(int pageIndex) {
    // Hide all page-specific controls
    for (HWND c : {themeLabel_, themeCombo_, densityLabel_, densityCombo_, previewCheck_, previewSizeLabel_, previewSizeEdit_,
                   searchScopeCombo_, retainHistoryCheck_, startupLabel_, startupPathEdit_, restoreSessionCheck_,
                   volumeList_, ruleList_, statusDetail_, addRuleButton_, excludeRuleCheck_, removeRuleButton_, moveUpButton_, moveDownButton_,
                   pauseAllCheck_, pauseResumeButton_, addToIndexingButton_,
                   categoryList_, addCategoryButton_, removeCategoryButton_,
                   shortcutList_, resetShortcutsButton_}) {
        if (c) ShowWindow(c, SW_HIDE);
    }
    
    // Show current page controls
    switch (pageIndex) {
        case 0: // General
            if (themeLabel_) ShowWindow(themeLabel_, SW_SHOW);
            if (themeCombo_) ShowWindow(themeCombo_, SW_SHOW);
            if (densityLabel_) ShowWindow(densityLabel_, SW_SHOW);
            if (densityCombo_) ShowWindow(densityCombo_, SW_SHOW);
            if (previewCheck_) ShowWindow(previewCheck_, SW_SHOW);
            if (previewSizeLabel_) ShowWindow(previewSizeLabel_, SW_SHOW);
            if (previewSizeEdit_) ShowWindow(previewSizeEdit_, SW_SHOW);
            break;
        case 1: // Search
            if (searchScopeCombo_) ShowWindow(searchScopeCombo_, SW_SHOW);
            if (retainHistoryCheck_) ShowWindow(retainHistoryCheck_, SW_SHOW);
            break;
        case 2: // Navigation
            if (startupLabel_) ShowWindow(startupLabel_, SW_SHOW);
            if (startupPathEdit_) ShowWindow(startupPathEdit_, SW_SHOW);
            if (restoreSessionCheck_) ShowWindow(restoreSessionCheck_, SW_SHOW);
            break;
        case 3: // Indexing
            if (volumeList_) ShowWindow(volumeList_, SW_SHOW);
            if (statusDetail_) ShowWindow(statusDetail_, SW_SHOW);
            if (addRuleButton_) ShowWindow(addRuleButton_, SW_SHOW);
            if (removeRuleButton_) ShowWindow(removeRuleButton_, SW_SHOW);
            if (moveUpButton_) ShowWindow(moveUpButton_, SW_SHOW);
            if (moveDownButton_) ShowWindow(moveDownButton_, SW_SHOW);
            if (pauseAllCheck_) ShowWindow(pauseAllCheck_, SW_SHOW);
            if (pauseResumeButton_) ShowWindow(pauseResumeButton_, SW_SHOW);
            if (addToIndexingButton_) ShowWindow(addToIndexingButton_, SW_SHOW);
            UpdateVolumeList();
            RefreshVolumeStatuses();
            break;
        case 4: // Storage
            if (categoryList_) ShowWindow(categoryList_, SW_SHOW);
            if (addCategoryButton_) ShowWindow(addCategoryButton_, SW_SHOW);
            if (removeCategoryButton_) ShowWindow(removeCategoryButton_, SW_SHOW);
            PopulateStoragePage();
            break;
        case 5: // Shortcuts
            if (shortcutList_) ShowWindow(shortcutList_, SW_SHOW);
            if (resetShortcutsButton_) ShowWindow(resetShortcutsButton_, SW_SHOW);
            PopulateShortcutsPage();
            break;
    }
}

void SettingsDialog::UpdateVolumeList() {
    if (!volumeList_ || !settings_) return;

    std::wstring selectedPath;
    const int oldSelected = ListView_GetNextItem(volumeList_, -1, LVNI_SELECTED);
    if (oldSelected >= 0 && oldSelected < static_cast<int>(volumes_.size())) {
        selectedPath = volumes_[static_cast<size_t>(oldSelected)].path;
    }
    
    volumes_.clear();
    const DWORD driveMask = GetLogicalDrives();
    for (wchar_t letter = L'A'; letter <= L'Z'; ++letter) {
        if ((driveMask & (1u << (letter - L'A'))) == 0) continue;
        wchar_t rootPath[] = {letter, L':', L'\\', L'\0'};
        // §9.3: removable volumes are listed too -- a newly attached
        // external drive is exactly the pending-decision case.
        const UINT driveType = GetDriveTypeW(rootPath);
        if (driveType != DRIVE_FIXED && driveType != DRIVE_REMOVABLE) continue;
        
        VolumeToggle vol;
        vol.path = rootPath;
        vol.displayName = rootPath;
        
        // Check if this volume is in settings
        for (const auto& vs : settingsDraft_.indexing) {
            if (vs.key == rootPath) {
                vol.indexed = vs.enabled;
                vol.inSelection = true;
                break;
            }
        }
        
        volumes_.push_back(vol);
    }
    
    ListView_DeleteAllItems(volumeList_);
    for (size_t i = 0; i < volumes_.size(); ++i) {
        LVITEMW item{};
        item.iItem = static_cast<int>(i);
        item.iSubItem = 0;
        item.mask = LVIF_TEXT;
        item.pszText = const_cast<wchar_t*>(volumes_[i].displayName.c_str());
        ListView_InsertItem(volumeList_, &item);

        ListView_SetCheckState(volumeList_, static_cast<int>(i), volumes_[i].indexed);
        if (!selectedPath.empty() && volumes_[i].path == selectedPath) {
            ListView_SetItemState(volumeList_, static_cast<int>(i), LVIS_SELECTED | LVIS_FOCUSED,
                                  LVIS_SELECTED | LVIS_FOCUSED);
        }
        const std::wstring statusText = StatusTextForVolume(i);
        if (!statusText.empty()) {
            ListView_SetItemText(volumeList_, static_cast<int>(i), 1,
                                 const_cast<wchar_t*>(statusText.c_str()));
        }
    }
    UpdateStatusExplanation(0);
    UpdateRuleList();
}

void SettingsDialog::UpdateRuleList() {
    if (!ruleList_ || !volumeList_ || !settings_) {
        return;
    }
    ListView_DeleteAllItems(ruleList_);

    const int selectedVolume = ListView_GetNextItem(volumeList_, -1, LVNI_SELECTED);
    if (selectedVolume < 0 || selectedVolume >= static_cast<int>(volumes_.size())) {
        return;
    }
    const auto setting = std::find_if(settingsDraft_.indexing.begin(), settingsDraft_.indexing.end(),
                                      [this, selectedVolume](const ffprotocol::VolumeSetting& value) {
                                          return value.key == volumes_[static_cast<size_t>(selectedVolume)].path;
                                      });
    if (setting == settingsDraft_.indexing.end()) {
        return;
    }

    for (size_t i = 0; i < setting->rules.size(); ++i) {
        const auto& rule = setting->rules[i];
        LVITEMW item{};
        item.iItem = static_cast<int>(i);
        item.mask = LVIF_TEXT;
        item.pszText = const_cast<wchar_t*>(rule.include ? L"Include" : L"Exclude");
        ListView_InsertItem(ruleList_, &item);
        ListView_SetItemText(ruleList_, static_cast<int>(i), 1,
                             const_cast<wchar_t*>(rule.path.c_str()));
    }
}

void SettingsDialog::SetEngineActive(bool active) {
    engineActive_ = active;
    // The headline badge and the per-volume status column share the same
    // connection-state input; re-derive whenever it changes.
    if (volumeList_ && IsWindowVisible(volumeList_)) {
        UpdateVolumeList();
    }
}

void SettingsDialog::ToggleGlobalPause(bool paused) {
    if (engineClient_) {
        engineClient_->SetIndexingPaused(0, paused);
        // Read the new state back through the §7.3 status report so the
        // checkbox and the per-volume column reflect the engine's actual
        // state (D9: status flows back through the derivation, not a
        // separate outcome field).
        RefreshVolumeStatuses();
    } else {
        // The command cannot reach the engine; do not flip local state.
        SendMessageW(pauseAllCheck_, BM_SETCHECK, BST_UNCHECKED, 0);
    }
}

void SettingsDialog::ToggleSelectedVolumePause() {
    if (!volumeList_ || !engineClient_) return;
    const int selected = ListView_GetNextItem(volumeList_, -1, LVNI_SELECTED);
    if (selected < 0 || selected >= static_cast<int>(volumes_.size())) return;
    const std::wstring& path = volumes_[selected].path;
    if (path.empty()) return;
    const uint8_t letter = static_cast<uint8_t>(towupper(path.front()));
    const auto it = volumeStatusFlags_.find(letter);
    const bool paused = (it != volumeStatusFlags_.end()) && (it->second & ffprotocol::VolumeStatusPaused) != 0;
    engineClient_->SetIndexingPaused(letter, !paused);
    RefreshVolumeStatuses();
}

void SettingsDialog::AddPendingVolumeToIndexing() {
    if (!settings_ || !volumeList_) return;
    const int selected = ListView_GetNextItem(volumeList_, -1, LVNI_SELECTED);
    if (selected < 0 || selected >= static_cast<int>(volumes_.size())) return;
    VolumeToggle& volume = volumes_[selected];
    if (volume.inSelection) return; // already decided; not pending

    // Record the inclusion in the draft; it is persisted and pushed to the
    // engine on OK (onChanged_ -> atomic settings write +
    // ReloadIndexingConfig), which then starts scanning the volume through
    // its normal session path -- no restart anywhere.
    ffprotocol::VolumeSetting vs;
    vs.key = volume.path;
    vs.enabled = true;
    settingsDraft_.indexing.push_back(std::move(vs));
    volume.inSelection = true;
    volume.indexed = true;
    ListView_SetCheckState(volumeList_, selected, TRUE);
    UpdateVolumeList();
}

void SettingsDialog::RefreshVolumeStatuses() {
    if (!engineClient_) {
        // No engine client (or engine not started yet): the status column
        // falls back to the connection-independent text below.
        UpdateVolumeList();
        return;
    }
    volumeStatusReceived_ = false;
    UpdateVolumeList();
    engineClient_->RequestVolumeStatus([this](std::vector<ffprotocol::VolumeStatusRecord> records) {
        auto owned = std::make_unique<std::vector<ffprotocol::VolumeStatusRecord>>(std::move(records));
        const HWND target = dialog_;
        if (target != nullptr
            && PostMessageW(target, WM_APP_VOLUME_STATUS, 0, reinterpret_cast<LPARAM>(owned.get()))) {
            owned.release();
        }
    });
}

void SettingsDialog::ApplyVolumeStatuses(const std::vector<ffprotocol::VolumeStatusRecord>& records) {
    volumeStatusFlags_.clear();
    for (const auto& record : records) {
        volumeStatusFlags_[record.driveLetter] = record.flags;
    }
    volumeStatusReceived_ = true;
    // §9.1: the global-pause checkbox mirrors the engine-reported state, not
    // the last local click (a failed/lost request must not look applied).
    if (pauseAllCheck_) {
        const bool allPaused = !records.empty() && std::all_of(
            records.begin(), records.end(),
            [](const ffprotocol::VolumeStatusRecord& record) {
                return (record.flags & ffprotocol::VolumeStatusPaused) != 0;
            });
        SendMessageW(pauseAllCheck_, BM_SETCHECK, allPaused ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    UpdateVolumeList();
}

std::wstring SettingsDialog::StatusTextForVolume(size_t volumeIndex) const {
    if (volumeIndex >= volumes_.size()) {
        return {};
    }
    const std::wstring& path = volumes_[volumeIndex].path;
    if (path.empty()) {
        return {};
    }
    const uint8_t letter = static_cast<uint8_t>(towupper(path.front()));
    auto it = volumeStatusFlags_.find(letter);
    const uint8_t flags = it == volumeStatusFlags_.end() ? 0 : it->second;

    // §9.3: an observed/unselected drive is pending a user decision.
    if (!volumes_[volumeIndex].inSelection) {
        return L"Pending decision";
    }
    // §9.2: a selected-but-disabled volume shows "Disabled", never
    // "Paused" (spec: distinct from a temporary suspension).
    if (!volumes_[volumeIndex].indexed) {
        return L"Disabled";
    }
    // §9.1: paused state is read back through the engine's status flags.
    if ((flags & ffprotocol::VolumeStatusPaused) != 0) {
        return L"Paused";
    }
    // Before the first status report arrives, flags are unknown — never
    // render a healthy volume as stale/unhealthy in that window.
    if (engineClient_ && !volumeStatusReceived_) {
        return L"Checking…";
    }

    ffprotocol::VolumeIndexConditions conditions;
    conditions.privilegedConnectionActive = engineActive_;
    conditions.reachable = (flags & ffprotocol::VolumeStatusReachable) != 0;
    conditions.scanning = (flags & ffprotocol::VolumeStatusScanning) != 0;
    conditions.needsReconciliation = (flags & ffprotocol::VolumeStatusNeedsReconciliation) != 0;
    conditions.partiallyIndexed = (flags & ffprotocol::VolumeStatusPartiallyIndexed) != 0;
    return ffprotocol::IndexHealthName(ffprotocol::DeriveIndexHealth(conditions));
}

void SettingsDialog::UpdateStatusExplanation(size_t volumeIndex) {
    if (!statusDetail_ || volumeIndex >= volumes_.size()) {
        return;
    }
    const std::wstring status = StatusTextForVolume(volumeIndex);
    if (status.empty()) {
        SetWindowTextW(statusDetail_, L"Status unavailable while the engine is starting.");
        return;
    }
    if (status == L"Pending decision") {
        SetWindowTextW(statusDetail_, L"This volume has no entry in the indexing configuration. Check the box or press \"Add to Indexing\" to include it.");
        return;
    }
    if (status == L"Disabled") {
        SetWindowTextW(statusDetail_, L"Indexing is disabled for this volume. Unchecking it stopped scanning; re-enable it to start again.");
        return;
    }
    if (status == L"Paused") {
        SetWindowTextW(statusDetail_, L"Indexing is paused for this volume. Resume continues from where it left off, without restarting from zero.");
        return;
    }
    if (status == L"Checking…") {
        SetWindowTextW(statusDetail_, L"Checking index status for this volume…");
        return;
    }

    const std::wstring& path = volumes_[volumeIndex].path;
    const uint8_t letter = static_cast<uint8_t>(towupper(path.front()));
    auto it = volumeStatusFlags_.find(letter);
    uint8_t flags = 0;
    if (it != volumeStatusFlags_.end()) {
        flags = it->second;
    }
    ffprotocol::VolumeIndexConditions conditions;
    conditions.privilegedConnectionActive = engineActive_;
    conditions.reachable = (flags & ffprotocol::VolumeStatusReachable) != 0;
    conditions.scanning = (flags & ffprotocol::VolumeStatusScanning) != 0;
    conditions.needsReconciliation = (flags & ffprotocol::VolumeStatusNeedsReconciliation) != 0;
    conditions.partiallyIndexed = (flags & ffprotocol::VolumeStatusPartiallyIndexed) != 0;

    const auto applicable = ffprotocol::ApplicableIndexConditions(conditions);
    std::wstring message;
    if (status == ffprotocol::IndexHealthName(ffprotocol::IndexHealth::FullyIndexed)) {
        message = L"Fully indexed: search results on this volume are current.";
    } else {
        message = L"Search results on this volume may be missing or stale: ";
        for (size_t i = 0; i < applicable.size(); ++i) {
            if (i > 0) message += L", ";
            message += ffprotocol::IndexConditionName(applicable[i]);
        }
        message += L".";
    }
    SetWindowTextW(statusDetail_, message.c_str());
}

void SettingsDialog::AddVolumeRule() {
    if (!settings_ || !navigationWorkspace_) return;
    std::wstring path = navigationWorkspace_->ActiveContext().currentPath;
    if (path.empty()) return;

    std::wstring volumeKey;
    const int selectedVolume = volumeList_ ? ListView_GetNextItem(volumeList_, -1, LVNI_SELECTED) : -1;
    if (selectedVolume >= 0 && selectedVolume < static_cast<int>(volumes_.size())) {
        volumeKey = volumes_[static_cast<size_t>(selectedVolume)].path;
    }
    if (volumeKey.empty()) {
        volumeKey = std::filesystem::path(path).root_path().wstring();
    }
    if (volumeKey.empty()) return;

    auto setting = std::find_if(settingsDraft_.indexing.begin(), settingsDraft_.indexing.end(),
                                [&volumeKey](const ffprotocol::VolumeSetting& value) {
                                    return value.key == volumeKey;
                                });
    if (setting == settingsDraft_.indexing.end()) {
        ffprotocol::VolumeSetting value;
        value.key = volumeKey;
        value.enabled = true;
        settingsDraft_.indexing.push_back(std::move(value));
        setting = std::prev(settingsDraft_.indexing.end());
    }

    const bool include = !excludeRuleCheck_ || SendMessageW(excludeRuleCheck_, BM_GETCHECK, 0, 0) != BST_CHECKED;
    const auto duplicate = std::find_if(setting->rules.begin(), setting->rules.end(),
                                        [&path, include](const ffprotocol::DirectoryRule& rule) {
                                            return rule.path == path && rule.include == include;
                                        });
    if (duplicate != setting->rules.end()) {
        UpdateRuleList();
        return;
    }

    ffprotocol::DirectoryRule rule;
    rule.path = path;
    rule.include = include;
    setting->rules.push_back(std::move(rule));

    UpdateRuleList();
}

void SettingsDialog::RemoveVolumeRule() {
    if (!settings_ || !volumeList_ || !ruleList_) return;
    const int selectedVolume = ListView_GetNextItem(volumeList_, -1, LVNI_SELECTED);
    const int selectedRule = ListView_GetNextItem(ruleList_, -1, LVNI_SELECTED);
    if (selectedVolume < 0 || selectedVolume >= static_cast<int>(volumes_.size()) || selectedRule < 0) return;
    const std::wstring& volumeKey = volumes_[static_cast<size_t>(selectedVolume)].path;
    const auto setting = std::find_if(settingsDraft_.indexing.begin(), settingsDraft_.indexing.end(),
                                      [&volumeKey](const ffprotocol::VolumeSetting& value) {
                                          return value.key == volumeKey;
                                      });
    if (setting == settingsDraft_.indexing.end() || selectedRule >= static_cast<int>(setting->rules.size())) return;
    setting->rules.erase(setting->rules.begin() + selectedRule);
    UpdateRuleList();
}

void SettingsDialog::MoveVolumeRuleUp() {
    if (!settings_ || !volumeList_ || !ruleList_) return;
    const int selectedVolume = ListView_GetNextItem(volumeList_, -1, LVNI_SELECTED);
    const int selectedRule = ListView_GetNextItem(ruleList_, -1, LVNI_SELECTED);
    if (selectedVolume < 0 || selectedVolume >= static_cast<int>(volumes_.size()) || selectedRule <= 0) return;
    const std::wstring& volumeKey = volumes_[static_cast<size_t>(selectedVolume)].path;
    const auto setting = std::find_if(settingsDraft_.indexing.begin(), settingsDraft_.indexing.end(),
                                      [&volumeKey](const ffprotocol::VolumeSetting& value) {
                                          return value.key == volumeKey;
                                      });
    if (setting == settingsDraft_.indexing.end() || selectedRule >= static_cast<int>(setting->rules.size())) return;
    std::iter_swap(setting->rules.begin() + selectedRule, setting->rules.begin() + selectedRule - 1);
    UpdateRuleList();
    ListView_SetItemState(ruleList_, selectedRule - 1, LVIS_SELECTED | LVIS_FOCUSED,
                          LVIS_SELECTED | LVIS_FOCUSED);
}

void SettingsDialog::MoveVolumeRuleDown() {
    if (!settings_ || !volumeList_ || !ruleList_) return;
    const int selectedVolume = ListView_GetNextItem(volumeList_, -1, LVNI_SELECTED);
    const int selectedRule = ListView_GetNextItem(ruleList_, -1, LVNI_SELECTED);
    if (selectedVolume < 0 || selectedVolume >= static_cast<int>(volumes_.size()) || selectedRule < 0) return;
    const std::wstring& volumeKey = volumes_[static_cast<size_t>(selectedVolume)].path;
    const auto setting = std::find_if(settingsDraft_.indexing.begin(), settingsDraft_.indexing.end(),
                                      [&volumeKey](const ffprotocol::VolumeSetting& value) {
                                          return value.key == volumeKey;
                                      });
    if (setting == settingsDraft_.indexing.end() || selectedRule + 1 >= static_cast<int>(setting->rules.size())) return;
    std::iter_swap(setting->rules.begin() + selectedRule, setting->rules.begin() + selectedRule + 1);
    UpdateRuleList();
    ListView_SetItemState(ruleList_, selectedRule + 1, LVIS_SELECTED | LVIS_FOCUSED,
                          LVIS_SELECTED | LVIS_FOCUSED);
}

void SettingsDialog::PopulateIndexingPage() {
    if (!volumeList_) {
        const int rightColumnX = Scaled(kDialogWidth - 2 * kPagePadding - 100);
        const int rightColumnWidth = Scaled(100);
        volumeList_ = CreateWindowExW(0, WC_LISTVIEWW, L"",
                                       WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | WS_TABSTOP,
                                       Scaled(kPagePadding), Scaled(kPagePadding + 30), Scaled(kDialogWidth) - 2 * Scaled(kPagePadding) - Scaled(100), Scaled(180),
                                       dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);
        ListView_SetExtendedListViewStyle(volumeList_, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
        // §7.3: Status column shows the derived per-volume index health.
        InsertColumn(volumeList_, 0, kDialogWidth - 2 * kPagePadding - 240, L"Volume");
        InsertColumn(volumeList_, 1, 130, L"Status");

        statusDetail_ = CreateWindowExW(0, L"STATIC", L"",
                                         WS_CHILD | WS_VISIBLE | SS_LEFT,
                                         Scaled(kPagePadding), Scaled(kPagePadding + 214), Scaled(kDialogWidth) - 2 * Scaled(kPagePadding) - Scaled(100), Scaled(44),
                                         dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);

        ruleList_ = CreateWindowExW(0, WC_LISTVIEWW, L"",
                                    WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | WS_TABSTOP,
                                    Scaled(kPagePadding), Scaled(kPagePadding + 292),
                                    Scaled(kDialogWidth) - 2 * Scaled(kPagePadding) - Scaled(128), Scaled(142),
                                    dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);
        ListView_SetExtendedListViewStyle(ruleList_, LVS_EX_FULLROWSELECT);
        InsertColumn(ruleList_, 0, 70, L"Rule");
        InsertColumn(ruleList_, 1, kDialogWidth - 2 * kPagePadding - 210, L"Path");

        // §9.1/§9.3: control-plane actions. Pause is transient engine
        // state; the button label toggles from the status flags.
        pauseAllCheck_ = CreateWindowExW(0, L"BUTTON", L"Pause indexing (all volumes)",
                                         WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                         Scaled(kPagePadding), Scaled(kPagePadding + 264), Scaled(220), Scaled(20),
                                         dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);

        addRuleButton_ = CreateWindowExW(0, L"BUTTON", L"Add Rule", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                          rightColumnX, Scaled(kPagePadding + 30), rightColumnWidth, Scaled(24),
                                          dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);
        excludeRuleCheck_ = CreateWindowExW(0, L"BUTTON", L"Exclude rule", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                            rightColumnX, Scaled(kPagePadding + 264), rightColumnWidth + Scaled(16), Scaled(20),
                                            dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);
        removeRuleButton_ = CreateWindowExW(0, L"BUTTON", L"Remove", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                             rightColumnX, Scaled(kPagePadding + 62), rightColumnWidth, Scaled(24),
                                             dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);
        moveUpButton_ = CreateWindowExW(0, L"BUTTON", L"Up", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                         rightColumnX, Scaled(kPagePadding + 94), rightColumnWidth, Scaled(24),
                                         dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);
        moveDownButton_ = CreateWindowExW(0, L"BUTTON", L"Down", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                           rightColumnX, Scaled(kPagePadding + 126), rightColumnWidth, Scaled(24),
                                           dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);
        pauseResumeButton_ = CreateWindowExW(0, L"BUTTON", L"Pause", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                              rightColumnX, Scaled(kPagePadding + 158), rightColumnWidth, Scaled(24),
                                              dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);
        addToIndexingButton_ = CreateWindowExW(0, L"BUTTON", L"Add to Indexing", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                rightColumnX, Scaled(kPagePadding + 190), rightColumnWidth, Scaled(24),
                                                dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);
    }
    UpdateVolumeList();
    RefreshVolumeStatuses();
}

void SettingsDialog::PopulateStoragePage() {
    if (!categoryList_) {
        categoryList_ = CreateWindowExW(0, WC_LISTVIEWW, L"",
                                         WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | WS_TABSTOP,
                                         Scaled(kPagePadding), Scaled(kPagePadding + 30), Scaled(kDialogWidth) - 2 * Scaled(kPagePadding) - Scaled(100), Scaled(200),
                                         dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);
        InsertColumn(categoryList_, 0, 150, L"Category");
        InsertColumn(categoryList_, 1, kDialogWidth - 2 * kPagePadding - 260, L"Extensions");
        ListView_SetExtendedListViewStyle(categoryList_, LVS_EX_FULLROWSELECT);
        
        addCategoryButton_ = CreateWindowExW(0, L"BUTTON", L"Add", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                              Scaled(kDialogWidth) - Scaled(90), Scaled(kPagePadding + 30), Scaled(80), Scaled(24),
                                              dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);
        removeCategoryButton_ = CreateWindowExW(0, L"BUTTON", L"Remove", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                 Scaled(kDialogWidth) - Scaled(90), Scaled(kPagePadding + 60), Scaled(80), Scaled(24),
                                                 dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);
    }
    
    ListView_DeleteAllItems(categoryList_);
    if (settings_) {
        for (size_t i = 0; i < settingsDraft_.storageCategories.size(); ++i) {
            LVITEMW item{};
            item.iItem = static_cast<int>(i);
            item.iSubItem = 0;
            item.mask = LVIF_TEXT;
            item.pszText = const_cast<wchar_t*>(settingsDraft_.storageCategories[i].first.c_str());
            ListView_InsertItem(categoryList_, &item);

            ListView_SetItemText(categoryList_, static_cast<int>(i), 1,
                                 const_cast<wchar_t*>(settingsDraft_.storageCategories[i].second.c_str()));
        }
    }
}

void SettingsDialog::PopulateShortcutsPage() {
    if (!shortcutList_) {
        shortcutList_ = CreateWindowExW(0, WC_LISTVIEWW, L"",
                                         WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | WS_TABSTOP,
                                         Scaled(kPagePadding), Scaled(kPagePadding + 30), Scaled(kDialogWidth) - 2 * Scaled(kPagePadding) - Scaled(100), Scaled(200),
                                         dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);
        InsertColumn(shortcutList_, 0, 200, L"Command");
        InsertColumn(shortcutList_, 1, 150, L"Shortcut");
        ListView_SetExtendedListViewStyle(shortcutList_, LVS_EX_FULLROWSELECT);
        
        resetShortcutsButton_ = CreateWindowExW(0, L"BUTTON", L"Reset Defaults", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                  Scaled(kDialogWidth) - Scaled(90), Scaled(kPagePadding + 30), Scaled(80), Scaled(24),
                                                  dialog_, reinterpret_cast<HMENU>(40001), GetModuleHandleW(nullptr), nullptr);
    }
    
    ListView_DeleteAllItems(shortcutList_);
    if (commandRegistry_ && shortcuts_) {
        for (size_t i = 0; i < commandRegistry_->Commands().size(); ++i) {
            const auto& command = commandRegistry_->Commands()[i];
            const auto* binding = shortcutDraft_.FindByCommand(command.commandId);
            LVITEMW item{};
            item.iItem = static_cast<int>(i);
            item.iSubItem = 0;
            item.mask = LVIF_TEXT;
            item.pszText = const_cast<wchar_t*>(command.displayName.c_str());
            ListView_InsertItem(shortcutList_, &item);
            
            const std::wstring shortcutText = binding ? FormatKeyChord(binding->chord) : L"(Unbound)";
            ListView_SetItemText(shortcutList_, static_cast<int>(i), 1,
                                 const_cast<wchar_t*>(shortcutText.c_str()));
        }
    }
}

void SettingsDialog::BeginShortcutEdit(int row) {
    if (!shortcutList_ || !commandRegistry_ || shortcutEdit_ != nullptr ||
        row < 0 || row >= static_cast<int>(commandRegistry_->Commands().size())) return;
    RECT bounds{};
    if (!ListView_GetSubItemRect(shortcutList_, row, 1, LVIR_BOUNDS, &bounds)) return;
    MapWindowPoints(shortcutList_, dialog_, reinterpret_cast<POINT*>(&bounds), 2);
    const auto& command = commandRegistry_->Commands()[static_cast<size_t>(row)];
    const auto* binding = shortcutDraft_.FindByCommand(command.commandId);
    const std::wstring text = binding ? FormatKeyChord(binding->chord) : L"";
    shortcutEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text.c_str(),
                                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                    bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
                                    dialog_, reinterpret_cast<HMENU>(40002), GetModuleHandleW(nullptr), nullptr);
    if (!shortcutEdit_) return;
    shortcutEditRow_ = row;
    shortcutEditCommand_ = command.commandId;
    SetWindowSubclass(shortcutEdit_, ShortcutEditProc, 1, reinterpret_cast<DWORD_PTR>(this));
    SendMessageW(shortcutEdit_, EM_SETSEL, 0, -1);
    SetFocus(shortcutEdit_);
}

void SettingsDialog::FinishShortcutEdit(bool accept) {
    if (!shortcutEdit_) return;
    const HWND editor = shortcutEdit_;
    const std::wstring commandId = shortcutEditCommand_;
    shortcutEdit_ = nullptr;
    shortcutEditCommand_.clear();
    shortcutEditRow_ = -1;
    const std::wstring text = GetWindowText(editor);
    RemoveWindowSubclass(editor, ShortcutEditProc, 1);
    DestroyWindow(editor);
    if (!accept || !shortcuts_ || !commandRegistry_) return;

    if (text.empty()) {
        shortcutDraft_.Remove(commandId);
        PopulateShortcutsPage();
        return;
    }
    const auto chord = ShortcutMap::ParseKeyChord(text);
    if (!chord) {
        MessageBoxW(dialog_, L"Enter a shortcut such as Ctrl+Shift+P, or leave it blank to unbind.",
                    L"Invalid shortcut", MB_OK | MB_ICONWARNING);
        PopulateShortcutsPage();
        return;
    }
    if (ShortcutMap::IsWindowsReserved(*chord)) {
        const int choice = MessageBoxW(dialog_, L"Windows reserves Alt+F4 and may intercept it before Fast Files receives it. Save this binding anyway?",
                                       L"Reserved shortcut", MB_YESNO | MB_ICONWARNING);
        if (choice != IDYES) {
            PopulateShortcutsPage();
            return;
        }
    }
    RebindResult result = shortcutDraft_.Rebind(commandId, *chord, false);
    if (result == RebindResult::Conflict) {
        const int choice = MessageBoxW(dialog_, L"That shortcut is already assigned. Reassign it?",
                                       L"Shortcut conflict", MB_YESNO | MB_ICONQUESTION);
        if (choice == IDYES) result = shortcutDraft_.Rebind(commandId, *chord, true);
    }
    if (result == RebindResult::InvalidBinding) {
        MessageBoxW(dialog_, L"That shortcut is not a valid key combination.",
                    L"Invalid shortcut", MB_OK | MB_ICONWARNING);
    }
    PopulateShortcutsPage();
}

LRESULT CALLBACK SettingsDialog::ShortcutEditProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                                  UINT_PTR, DWORD_PTR referenceData) {
    auto* self = reinterpret_cast<SettingsDialog*>(referenceData);
    if (self != nullptr && message == WM_KEYDOWN) {
        if (wParam == VK_RETURN) {
            self->FinishShortcutEdit(true);
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            self->FinishShortcutEdit(false);
            return 0;
        }
    }
    if (self != nullptr && message == WM_KILLFOCUS && self->shortcutEdit_ == hwnd) {
        self->FinishShortcutEdit(true);
        return 0;
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

void SettingsDialog::SaveCurrentPage() {
    if (!settings_) return;

    // Save-all-on-OK: every page's controls persist for the dialog lifetime,
    // so all of them are read regardless of the visible tab. Everything is
    // written into the draft; IDOK commits it to *settings_.
    if (themeCombo_ && previewCheck_ && previewSizeEdit_) {
        switch (SendMessageW(themeCombo_, CB_GETCURSEL, 0, 0)) {
            case 0: settingsDraft_.theme = ffprotocol::ThemePreference::Light; break;
            case 1: settingsDraft_.theme = ffprotocol::ThemePreference::Dark; break;
            case 2: settingsDraft_.theme = ffprotocol::ThemePreference::FollowSystem; break;
            default: break;
        }
        if (densityCombo_) {
            settingsDraft_.density = SendMessageW(densityCombo_, CB_GETCURSEL, 0, 0) == 1
                ? ffprotocol::DensityPreference::Comfortable
                : ffprotocol::DensityPreference::Compact;
        }
        settingsDraft_.previewEnabled = SendMessageW(previewCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
        const std::wstring sizeText = GetWindowText(previewSizeEdit_);
        if (!sizeText.empty()) {
            unsigned long long parsed = settingsDraft_.maxAutoPreviewBytes;
            try { parsed = std::stoull(sizeText); } catch (const std::exception&) {}
            if (parsed > 0) settingsDraft_.maxAutoPreviewBytes = parsed;
        }
    }

    if (searchScopeCombo_ && retainHistoryCheck_) {
        const int scopeIndex = static_cast<int>(SendMessageW(searchScopeCombo_, CB_GETCURSEL, 0, 0));
        switch (scopeIndex) {
            case 0: settingsDraft_.defaultSearchScope = L"Current Folder"; break;
            case 1: settingsDraft_.defaultSearchScope = L"Current Folder + Subfolders"; break;
            case 2: settingsDraft_.defaultSearchScope = L"Current Drive"; break;
            case 3: settingsDraft_.defaultSearchScope = L"All Indexed Locations"; break;
            default: settingsDraft_.defaultSearchScope = L"Everywhere"; break;
        }
        settingsDraft_.retainSearchHistory = SendMessageW(retainHistoryCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }

    if (startupPathEdit_ && restoreSessionCheck_) {
        settingsDraft_.startupLocation = GetWindowText(startupPathEdit_);
        settingsDraft_.restorePreviousSession = SendMessageW(restoreSessionCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }

    if (volumeList_) {
        // Merge rather than rebuild: volumes currently attached are updated
        // from the checkbox list, while entries for configured-but-unattached
        // drives survive untouched -- only an explicit user action removes a
        // volume's configuration.
        for (size_t i = 0; i < volumes_.size(); ++i) {
            const bool enabled = ListView_GetCheckState(volumeList_, static_cast<int>(i)) != FALSE;
            const auto existing = std::find_if(settingsDraft_.indexing.begin(), settingsDraft_.indexing.end(),
                                               [this, i](const ffprotocol::VolumeSetting& value) {
                                                   return value.key == volumes_[i].path;
                                               });
            if (existing != settingsDraft_.indexing.end()) {
                existing->enabled = enabled;
            } else {
                ffprotocol::VolumeSetting vs;
                vs.key = volumes_[i].path;
                vs.enabled = enabled;
                settingsDraft_.indexing.push_back(std::move(vs));
            }
        }
    }

    if (categoryList_) {
        settingsDraft_.storageCategories.clear();
        const int count = ListView_GetItemCount(categoryList_);
        for (int i = 0; i < count; ++i) {
            wchar_t name[256]{};
            wchar_t exts[1024]{};
            ListView_GetItemText(categoryList_, i, 0, name, std::size(name));
            ListView_GetItemText(categoryList_, i, 1, exts, std::size(exts));
            settingsDraft_.storageCategories.emplace_back(name, exts);
        }
    }

    if (shortcutList_ && shortcuts_) {
        FinishShortcutEdit(true);
        *shortcuts_ = shortcutDraft_;
    }
}

void SettingsDialog::BeginCategoryEdit(int row, int column) {
    if (!categoryList_ || categoryEdit_ != nullptr ||
        row < 0 || row >= ListView_GetItemCount(categoryList_) ||
        (column != 0 && column != 1)) return;
    RECT bounds{};
    if (!ListView_GetSubItemRect(categoryList_, row, column, LVIR_BOUNDS, &bounds)) return;
    MapWindowPoints(categoryList_, dialog_, reinterpret_cast<POINT*>(&bounds), 2);
    wchar_t text[1024]{};
    ListView_GetItemText(categoryList_, row, column, text, std::size(text));
    categoryEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text,
                                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                    bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
                                    dialog_, reinterpret_cast<HMENU>(40003), GetModuleHandleW(nullptr), nullptr);
    if (!categoryEdit_) return;
    categoryEditRow_ = row;
    categoryEditColumn_ = column;
    SetWindowSubclass(categoryEdit_, CategoryEditProc, 2, reinterpret_cast<DWORD_PTR>(this));
    SendMessageW(categoryEdit_, EM_SETSEL, 0, -1);
    SetFocus(categoryEdit_);
}

void SettingsDialog::FinishCategoryEdit(bool accept) {
    if (!categoryEdit_) return;
    const HWND editor = categoryEdit_;
    const int row = categoryEditRow_;
    const int column = categoryEditColumn_;
    categoryEdit_ = nullptr;
    categoryEditRow_ = -1;
    categoryEditColumn_ = -1;
    const std::wstring text = GetWindowText(editor);
    RemoveWindowSubclass(editor, CategoryEditProc, 2);
    DestroyWindow(editor);
    if (!accept || !categoryList_ || row < 0 || row >= ListView_GetItemCount(categoryList_)) return;
    ListView_SetItemText(categoryList_, row, column, const_cast<wchar_t*>(text.c_str()));
}

LRESULT CALLBACK SettingsDialog::CategoryEditProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                                  UINT_PTR, DWORD_PTR referenceData) {
    auto* self = reinterpret_cast<SettingsDialog*>(referenceData);
    if (self != nullptr && message == WM_KEYDOWN) {
        if (wParam == VK_RETURN) {
            self->FinishCategoryEdit(true);
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            self->FinishCategoryEdit(false);
            return 0;
        }
    }
    if (self != nullptr && message == WM_KILLFOCUS && self->categoryEdit_ == hwnd) {
        self->FinishCategoryEdit(true);
        return 0;
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

void SettingsDialog::ResetShortcuts() {
    if (!commandRegistry_ || !shortcuts_) return;
    shortcutDraft_.ResetDefaults(*commandRegistry_);
}

void SettingsDialog::CenterWindow(HWND child, HWND parent) {
    RECT childRect{}, parentRect{};
    GetWindowRect(child, &childRect);
    GetWindowRect(parent, &parentRect);
    int width = childRect.right - childRect.left;
    int height = childRect.bottom - childRect.top;
    int x = parentRect.left + (parentRect.right - parentRect.left - width) / 2;
    int y = parentRect.top + (parentRect.bottom - parentRect.top - height) / 2;
    SetWindowPos(child, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOSIZE);
}

} // namespace ffui
