#pragma once

#include <functional>
#include <string>
#include <vector>
#include <windows.h>

#include "NavigationWorkspace.h"

namespace ffui {

class NavigationSidebar {
public:
    struct NavigationTarget {
        std::wstring path;
        std::wstring displayName;
        bool openInNewTab = false;
        // quick-access-home 5.1: true when the target is the Home surface (its
        // path is empty; checked by the shell before the filesystem branches).
        bool isHome = false;
    };
    using NavigateHandler = std::function<void(const NavigationTarget&)>;

    bool Initialize(HWND owner, NavigationWorkspace* workspace, NavigateHandler navigate);
    void Reposition();
    void Refresh();
    int Width() const;
    void SetDarkTheme(bool dark);
    // quick-access-home 5.1: invoked after the context-menu bookmark edits
    // (rename/remove/reorder) commit so the shell can refresh the Home surface.
    void SetBookmarksChangedCallback(std::function<void()> cb);

private:
    enum class RowKind { Section, Item };
    struct Row { RowKind kind; int section; size_t item; RECT bounds{}; std::wstring label; std::wstring path; bool home = false; };

    static LRESULT CALLBACK WindowProcThunk(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    void RebuildRows();
    void Draw(HDC dc, const RECT& client);
    void ToggleSection(int section);

    HWND owner_ = nullptr;
    HWND window_ = nullptr;
    NavigationWorkspace* workspace_ = nullptr;
    NavigateHandler navigate_;
    std::function<void()> bookmarksChanged_;
    std::vector<Row> rows_;
    bool darkTheme_ = false;
    int hoveredRow_ = -1;
};

} // namespace ffui
