#pragma once

#include "UITheme.h"

#include <windows.h>

namespace ffui {

// Tokenized palettes for owner-painted SysHeader32 and TabCtrl chrome.
// High Contrast keeps the system header/tab so the accessibility palette
// is never overridden. Consumed by NativeChrome.h and headless tests.

struct NativeHeaderPalette {
    COLORREF background = 0;
    COLORREF text = 0;
    COLORREF divider = 0;
    COLORREF sortGlyph = 0;
    bool useSystemHeader = false;
};

inline NativeHeaderPalette ResolveNativeHeaderPalette(bool dark, bool highContrast) {
    NativeHeaderPalette palette{};
    if (highContrast) {
        palette.useSystemHeader = true;
        palette.background = GetSysColor(COLOR_3DFACE);
        palette.text = GetSysColor(COLOR_BTNTEXT);
        palette.divider = GetSysColor(COLOR_3DSHADOW);
        palette.sortGlyph = palette.text;
        return palette;
    }
    const UiTheme theme = GetUiTheme(dark);
    palette.background = ToColorRef(theme.surfaceSubtle);
    palette.text = ToColorRef(theme.textSecondary);
    palette.divider = ToColorRef(theme.border);
    palette.sortGlyph = ToColorRef(theme.textTertiary);
    return palette;
}

struct NativeTabPalette {
    COLORREF stripBackground = 0;
    COLORREF selectedFill = 0;
    COLORREF selectedText = 0;
    COLORREF unselectedText = 0;
    COLORREF accentBar = 0;
    COLORREF focusStroke = 0;
    bool useSystemTab = false;
};

inline NativeTabPalette ResolveNativeTabPalette(bool dark, bool highContrast) {
    NativeTabPalette palette{};
    if (highContrast) {
        palette.useSystemTab = true;
        palette.stripBackground = GetSysColor(COLOR_3DFACE);
        palette.selectedFill = GetSysColor(COLOR_WINDOW);
        palette.selectedText = GetSysColor(COLOR_WINDOWTEXT);
        palette.unselectedText = GetSysColor(COLOR_BTNTEXT);
        palette.accentBar = GetSysColor(COLOR_HIGHLIGHT);
        palette.focusStroke = GetSysColor(COLOR_WINDOWTEXT);
        return palette;
    }
    const UiTheme theme = GetUiTheme(dark);
    palette.stripBackground = ToColorRef(theme.background);
    palette.selectedFill = ToColorRef(theme.surfaceElevated);
    palette.selectedText = ToColorRef(theme.text);
    palette.unselectedText = ToColorRef(theme.textSecondary);
    palette.accentBar = ToColorRef(theme.accent);
    palette.focusStroke = ToColorRef(theme.focusStroke);
    return palette;
}

}  // namespace ffui
