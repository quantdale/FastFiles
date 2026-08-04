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
    };
    using NavigateHandler = std::function<void(const NavigationTarget&)>;

    bool Initialize(HWND owner, NavigationWorkspace* workspace, NavigateHandler navigate);
    void Reposition();
    void Refresh();
    int Width() const;
    void SetDarkTheme(bool dark);

private:
    enum class RowKind { Section, Item };
    struct Row { RowKind kind; int section; size_t item; RECT bounds{}; std::wstring label; std::wstring path; };

    static LRESULT CALLBACK WindowProcThunk(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    void RebuildRows();
    void Draw(HDC dc, const RECT& client);
    void ToggleSection(int section);

    HWND owner_ = nullptr;
    HWND window_ = nullptr;
    NavigationWorkspace* workspace_ = nullptr;
    NavigateHandler navigate_;
    std::vector<Row> rows_;
    bool darkTheme_ = false;
    int hoveredRow_ = -1;
};

} // namespace ffui
