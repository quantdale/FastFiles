#include "SettingsDialog.h"

#include <commctrl.h>
#include <filesystem>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

namespace ffui {
namespace {

constexpr UINT WM_APP_SETTINGS_CHANGED = WM_APP + 20;
constexpr int kDialogWidth = 620;
constexpr int kDialogHeight = 520;
constexpr int kTabControlHeight = 28;
constexpr int kPageTop = 36;
constexpr int kPagePadding = 12;
constexpr int kControlHeight = 24;
constexpr int kControlSpacing = 32;

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

} // namespace

SettingsDialog::~SettingsDialog() {
    Hide();
}

bool SettingsDialog::Initialize(HWND owner, ffprotocol::Settings* settings,
                                NavigationWorkspace* navigationWorkspace,
                                std::function<void()> onChanged) {
    owner_ = owner;
    settings_ = settings;
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
                              0, 0, kDialogWidth, kDialogHeight,
                              owner_, nullptr, GetModuleHandleW(nullptr), this);
    if (!dialog_) return false;

    CenterWindow(dialog_, owner_);

    tabControl_ = CreateWindowExW(0, WC_TABCONTROLW, L"",
                                  WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                  kPagePadding, kPagePadding, kDialogWidth - 2 * kPagePadding, kTabControlHeight,
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

    SwitchPage(0);
    ShowWindow(dialog_, SW_SHOW);
    visible_ = true;
    return true;
}

void SettingsDialog::Show() {
    if (dialog_) {
        ShowWindow(dialog_, SW_SHOW);
        SetForegroundWindow(dialog_);
        visible_ = true;
    }
}

void SettingsDialog::Hide() {
    if (dialog_) {
        ShowWindow(dialog_, SW_HIDE);
        visible_ = false;
    }
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
                    SaveCurrentPage();
                    if (onChanged_) onChanged_();
                    Hide();
                    return 0;
                case IDCANCEL:
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
                if (lParam == reinterpret_cast<LPARAM>(addCategoryButton_)) {
                    // Add category placeholder
                    if (settings_) {
                        settings_->storageCategories.emplace_back(L"New Category", L"");
                        PopulateStoragePage();
                        if (onChanged_) onChanged_();
                    }
                    return 0;
                }
                if (lParam == reinterpret_cast<LPARAM>(removeCategoryButton_)) {
                    if (!categoryList_ || !settings_) return 0;
                    const int selected = ListView_GetNextItem(categoryList_, -1, LVNI_SELECTED);
                    if (selected >= 0 && selected < static_cast<int>(settings_->storageCategories.size())) {
                        settings_->storageCategories.erase(settings_->storageCategories.begin() + selected);
                        PopulateStoragePage();
                        if (onChanged_) onChanged_();
                    }
                    return 0;
                }
            }
            break;
        case WM_CLOSE:
            Hide();
            return 0;
        case WM_NOTIFY: {
            auto* hdr = reinterpret_cast<NMHDR*>(lParam);
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
                        }
                    }
                }
            }
            break;
        }
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void SettingsDialog::CreateControls(HWND hwnd) {
    (void)hwnd;
    // OK/Cancel buttons
    CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                    kDialogWidth - 180, kDialogHeight - 36, 80, 24,
                    dialog_, reinterpret_cast<HMENU>(IDOK), GetModuleHandleW(nullptr), nullptr);
    CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    kDialogWidth - 90, kDialogHeight - 36, 80, 24,
                    dialog_, reinterpret_cast<HMENU>(IDCANCEL), GetModuleHandleW(nullptr), nullptr);
}

void SettingsDialog::PopulateGeneralPage() {
}

void SettingsDialog::PopulateSearchPage() {
    if (!searchScopeCombo_) {
        searchScopeCombo_ = CreateWindowExW(0, L"COMBOBOX", L"",
                                             WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                             kPagePadding + 100, kPagePadding + 30, 200, 200,
                                             dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(searchScopeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Current Folder"));
        SendMessageW(searchScopeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Current Folder + Subfolders"));
        SendMessageW(searchScopeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Current Drive"));
        SendMessageW(searchScopeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"All Indexed Locations"));
    }
    if (!retainHistoryCheck_) {
        retainHistoryCheck_ = CreateWindowExW(0, L"BUTTON", L"Retain search history",
                                              WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                              kPagePadding + 100, kPagePadding + 60, 200, 20,
                                              dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);
    }

    int scopeIndex = 0;
    if (settings_) {
        const std::wstring& scope = settings_->defaultSearchScope;
        if (scope == L"Current Folder") scopeIndex = 0;
        else if (scope == L"Current Folder + Subfolders") scopeIndex = 1;
        else if (scope == L"Current Drive") scopeIndex = 2;
        else if (scope == L"All Indexed Locations") scopeIndex = 3;
    }
    SendMessageW(searchScopeCombo_, CB_SETCURSEL, scopeIndex, 0);
    SendMessageW(retainHistoryCheck_, BM_SETCHECK, settings_ && settings_->retainSearchHistory ? BST_CHECKED : BST_UNCHECKED, 0);
}

void SettingsDialog::PopulateNavigationPage() {
    if (!startupPathEdit_) {
        CreateWindowExW(0, L"STATIC", L"Startup location:", WS_CHILD | WS_VISIBLE,
                        kPagePadding + 100, kPagePadding + 30, 100, 20,
                        dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);
        startupPathEdit_ = CreateWindowExW(0, L"EDIT", L"",
                                           WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                           kPagePadding + 100, kPagePadding + 55, 300, 24,
                                           dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);
    }
    if (!restoreSessionCheck_) {
        restoreSessionCheck_ = CreateWindowExW(0, L"BUTTON", L"Restore previous session on startup",
                                               WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                               kPagePadding + 100, kPagePadding + 85, 250, 20,
                                               dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);
    }

    if (settings_) {
        SetWindowTextW(startupPathEdit_, settings_->startupLocation.c_str());
        SendMessageW(restoreSessionCheck_, BM_SETCHECK, settings_->restorePreviousSession ? BST_CHECKED : BST_UNCHECKED, 0);
    }
}

void SettingsDialog::SwitchPage(int pageIndex) {
    currentPage_ = pageIndex;
    // Hide all page-specific controls
    for (HWND c : {searchScopeCombo_, retainHistoryCheck_, startupPathEdit_, restoreSessionCheck_,
                   volumeList_, addRuleButton_, removeRuleButton_, moveUpButton_, moveDownButton_,
                   categoryList_, addCategoryButton_, removeCategoryButton_,
                   shortcutList_, resetShortcutsButton_}) {
        if (c) ShowWindow(c, SW_HIDE);
    }
    
    // Show current page controls
    switch (pageIndex) {
        case 1: // Search
            if (searchScopeCombo_) ShowWindow(searchScopeCombo_, SW_SHOW);
            if (retainHistoryCheck_) ShowWindow(retainHistoryCheck_, SW_SHOW);
            break;
        case 2: // Navigation
            if (startupPathEdit_) ShowWindow(startupPathEdit_, SW_SHOW);
            if (restoreSessionCheck_) ShowWindow(restoreSessionCheck_, SW_SHOW);
            break;
        case 3: // Indexing
            if (volumeList_) ShowWindow(volumeList_, SW_SHOW);
            if (addRuleButton_) ShowWindow(addRuleButton_, SW_SHOW);
            if (removeRuleButton_) ShowWindow(removeRuleButton_, SW_SHOW);
            if (moveUpButton_) ShowWindow(moveUpButton_, SW_SHOW);
            if (moveDownButton_) ShowWindow(moveDownButton_, SW_SHOW);
            UpdateVolumeList();
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
    
    volumes_.clear();
    const DWORD driveMask = GetLogicalDrives();
    for (wchar_t letter = L'A'; letter <= L'Z'; ++letter) {
        if ((driveMask & (1u << (letter - L'A'))) == 0) continue;
        wchar_t rootPath[] = {letter, L':', L'\\', L'\0'};
        if (GetDriveTypeW(rootPath) != DRIVE_FIXED) continue;
        
        VolumeToggle vol;
        vol.path = rootPath;
        vol.displayName = rootPath;
        
        // Check if this volume is in settings
        for (const auto& vs : settings_->indexing) {
            if (vs.key == rootPath) {
                vol.indexed = vs.enabled;
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
    }
}

void SettingsDialog::AddVolumeRule() {
    if (!settings_ || !navigationWorkspace_) return;
    std::wstring path = navigationWorkspace_->ActiveContext().currentPath;
    if (path.empty()) return;
    
    // Add rule to first indexed volume or create new
    if (settings_->indexing.empty()) {
        ffprotocol::VolumeSetting vs;
        vs.key = std::filesystem::path(path).root_path().wstring();
        vs.enabled = true;
        settings_->indexing.push_back(vs);
    }
    
    ffprotocol::DirectoryRule rule;
    rule.path = path;
    rule.include = true;
    settings_->indexing.back().rules.push_back(rule);
    
    if (onChanged_) onChanged_();
}

void SettingsDialog::RemoveVolumeRule() {
    // Remove selected rule
}

void SettingsDialog::MoveVolumeRuleUp() {
    // Move selected rule up
}

void SettingsDialog::MoveVolumeRuleDown() {
    // Move selected rule down
}

void SettingsDialog::PopulateIndexingPage() {
    if (!volumeList_) {
        volumeList_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                       WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | WS_TABSTOP,
                                       kPagePadding, kPagePadding + 30, kDialogWidth - 2 * kPagePadding - 100, 200,
                                       dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);
        ListView_SetExtendedListViewStyle(volumeList_, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
        InsertColumn(volumeList_, 0, kDialogWidth - 2 * kPagePadding - 140, L"Volume");
        
        addRuleButton_ = CreateWindowExW(0, L"BUTTON", L"Add Rule", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                          kDialogWidth - 90, kPagePadding + 30, 80, 24,
                                          dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);
        removeRuleButton_ = CreateWindowExW(0, L"BUTTON", L"Remove", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                             kDialogWidth - 90, kPagePadding + 60, 80, 24,
                                             dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);
        moveUpButton_ = CreateWindowExW(0, L"BUTTON", L"Up", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                         kDialogWidth - 90, kPagePadding + 90, 80, 24,
                                         dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);
        moveDownButton_ = CreateWindowExW(0, L"BUTTON", L"Down", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                           kDialogWidth - 90, kPagePadding + 120, 80, 24,
                                           dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);
    }
    UpdateVolumeList();
}

void SettingsDialog::PopulateStoragePage() {
    if (!categoryList_) {
        categoryList_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                         WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | WS_TABSTOP,
                                         kPagePadding, kPagePadding + 30, kDialogWidth - 2 * kPagePadding - 100, 200,
                                         dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);
        InsertColumn(categoryList_, 0, 150, L"Category");
        InsertColumn(categoryList_, 1, kDialogWidth - 2 * kPagePadding - 260, L"Extensions");
        ListView_SetExtendedListViewStyle(categoryList_, LVS_EX_FULLROWSELECT);
        
        addCategoryButton_ = CreateWindowExW(0, L"BUTTON", L"Add", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                              kDialogWidth - 90, kPagePadding + 30, 80, 24,
                                              dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);
        removeCategoryButton_ = CreateWindowExW(0, L"BUTTON", L"Remove", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                 kDialogWidth - 90, kPagePadding + 60, 80, 24,
                                                 dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);
    }
    
    ListView_DeleteAllItems(categoryList_);
    if (settings_) {
        for (size_t i = 0; i < settings_->storageCategories.size(); ++i) {
            LVITEMW item{};
            item.iItem = static_cast<int>(i);
            item.iSubItem = 0;
            item.mask = LVIF_TEXT;
            item.pszText = const_cast<wchar_t*>(settings_->storageCategories[i].first.c_str());
            ListView_InsertItem(categoryList_, &item);
            
            ListView_SetItemText(categoryList_, static_cast<int>(i), 1,
                                 const_cast<wchar_t*>(settings_->storageCategories[i].second.c_str()));
        }
    }
}

void SettingsDialog::PopulateShortcutsPage() {
    if (!shortcutList_) {
        shortcutList_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                         WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | WS_TABSTOP,
                                         kPagePadding, kPagePadding + 30, kDialogWidth - 2 * kPagePadding - 100, 200,
                                         dialog_, nullptr, GetModuleHandleW(nullptr), nullptr);
        InsertColumn(shortcutList_, 0, 200, L"Command");
        InsertColumn(shortcutList_, 1, 150, L"Shortcut");
        ListView_SetExtendedListViewStyle(shortcutList_, LVS_EX_FULLROWSELECT);
        
        resetShortcutsButton_ = CreateWindowExW(0, L"BUTTON", L"Reset Defaults", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                 kDialogWidth - 90, kPagePadding + 30, 80, 24,
                                                 dialog_, reinterpret_cast<HMENU>(40001), GetModuleHandleW(nullptr), nullptr);
    }
    
    ListView_DeleteAllItems(shortcutList_);
    if (settings_) {
        for (size_t i = 0; i < settings_->shortcuts.size(); ++i) {
            LVITEMW item{};
            item.iItem = static_cast<int>(i);
            item.iSubItem = 0;
            item.mask = LVIF_TEXT;
            item.pszText = const_cast<wchar_t*>(settings_->shortcuts[i].first.c_str());
            ListView_InsertItem(shortcutList_, &item);
            
            ListView_SetItemText(shortcutList_, static_cast<int>(i), 1,
                                 const_cast<wchar_t*>(settings_->shortcuts[i].second.c_str()));
        }
    }
}

void SettingsDialog::SaveCurrentPage() {
    if (!settings_) return;

    if (currentPage_ == 1 && searchScopeCombo_ && retainHistoryCheck_) {
        const int scopeIndex = static_cast<int>(SendMessageW(searchScopeCombo_, CB_GETCURSEL, 0, 0));
        switch (scopeIndex) {
            case 0: settings_->defaultSearchScope = L"Current Folder"; break;
            case 1: settings_->defaultSearchScope = L"Current Folder + Subfolders"; break;
            case 2: settings_->defaultSearchScope = L"Current Drive"; break;
            case 3: settings_->defaultSearchScope = L"All Indexed Locations"; break;
            default: settings_->defaultSearchScope = L"Everywhere"; break;
        }
        settings_->retainSearchHistory = SendMessageW(retainHistoryCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }

    if (currentPage_ == 2 && startupPathEdit_ && restoreSessionCheck_) {
        settings_->startupLocation = GetWindowText(startupPathEdit_);
        settings_->restorePreviousSession = SendMessageW(restoreSessionCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }

    if (currentPage_ == 3 && volumeList_) {
        settings_->indexing.clear();
        for (size_t i = 0; i < volumes_.size(); ++i) {
            ffprotocol::VolumeSetting vs;
            vs.key = volumes_[i].path;
            vs.enabled = ListView_GetCheckState(volumeList_, static_cast<int>(i)) != FALSE;
            // Preserve existing directory rules for this volume
            for (const auto& existing : settings_->indexing) {
                if (existing.key == vs.key) {
                    vs.rules = existing.rules;
                    break;
                }
            }
            settings_->indexing.push_back(vs);
        }
    }

    if (currentPage_ == 4 && categoryList_) {
        settings_->storageCategories.clear();
        const int count = ListView_GetItemCount(categoryList_);
        for (int i = 0; i < count; ++i) {
            wchar_t name[256]{};
            wchar_t exts[1024]{};
            ListView_GetItemText(categoryList_, i, 0, name, std::size(name));
            ListView_GetItemText(categoryList_, i, 1, exts, std::size(exts));
            settings_->storageCategories.emplace_back(name, exts);
        }
    }

    if (currentPage_ == 5 && shortcutList_) {
        settings_->shortcuts.clear();
        const int count = ListView_GetItemCount(shortcutList_);
        for (int i = 0; i < count; ++i) {
            wchar_t cmd[256]{};
            wchar_t key[256]{};
            ListView_GetItemText(shortcutList_, i, 0, cmd, std::size(cmd));
            ListView_GetItemText(shortcutList_, i, 1, key, std::size(key));
            settings_->shortcuts.emplace_back(cmd, key);
        }
    }
}

void SettingsDialog::ResetShortcuts() {
    if (!settings_) return;
    settings_->shortcuts.clear();
    // Defaults will be reloaded by CommandSystem on next startup
    if (onChanged_) onChanged_();
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
