#pragma once

#include <string>
#include <optional>
#include <vector>
#include <windows.h>

namespace ffui {

bool CopyTextToClipboard(HWND owner, const std::wstring& text);
bool CopyPathsToClipboard(HWND owner, const std::vector<std::wstring>& paths);
// Publishes a shell-compatible file list. `cut` uses the standard
// Preferred DropEffect clipboard format so Explorer and other shell clients
// can preserve move semantics; CF_HDROP remains the portable fallback.
bool CopyFilesToClipboard(HWND owner, const std::vector<std::wstring>& paths, bool cut);
// Reads a shell file list without retaining clipboard-owned memory. The
// returned flag is true when the source requested a move/cut operation.
std::vector<std::wstring> ReadFilesFromClipboard(HWND owner, bool& cut);
// Reads a CF_HDROP HGLOBAL (and an optional Preferred DropEffect HGLOBAL)
// through locked views, matching the write path and the OLE drop extractor.
// Returns false when the drop handle is null or cannot be locked; `paths` is
// then empty and `cut` is false. Exposed so tests can exercise the lock
// pairing without touching the process-global clipboard
// (lock-clipboard-hdrop-read change).
bool ReadDropHandle(HGLOBAL dropHandle, HGLOBAL preferredEffectHandle,
                    std::vector<std::wstring>& paths, bool& cut);
bool HasFilesOnClipboard(HWND owner);
// A clipboard manager may preserve CF_HDROP but remove Preferred DropEffect.
// The shell can recover the app's own cut intent only when the clipboard
// sequence and path list still match the publication made by FastFiles.
bool CanUseLocalCutClipboardFallback(const std::vector<std::wstring>& clipboardPaths,
                                     const std::vector<std::wstring>& localPaths,
                                     bool externalCut, bool localCut,
                                     DWORD currentSequenceNumber,
                                     DWORD localSequenceNumber) noexcept;
std::vector<std::wstring> PathsRelativeTo(const std::vector<std::wstring>& paths,
                                          const std::wstring& base, bool& usedAbsoluteFallback);
bool OpenWithDefaultApplication(HWND owner, const std::wstring& path);
bool ShowOpenWithPicker(HWND owner, const std::wstring& path);

struct ProcessLaunchSpec {
    std::wstring executable;
    std::wstring commandLine;
    std::wstring currentDirectory;
};

ProcessLaunchSpec BuildTerminalLaunchSpec(const std::wstring& targetDirectory,
                                          const std::wstring& preferredShell = L"powershell.exe");
bool LaunchTerminalHere(HWND owner, const std::wstring& targetDirectory);
std::optional<std::wstring> PromptForLeafName(HWND owner, const std::wstring& title,
                                              const std::wstring& initialValue);

} // namespace ffui
