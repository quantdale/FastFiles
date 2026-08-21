#include "FileOperations.h"

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <windows.h>
#include <winioctl.h>
#include <objbase.h>
#include <aclapi.h>
#include "../TestSupport.h"

using namespace fftest;

namespace {

std::unique_ptr<ffui::FileOperationEvent> lastEvent;
ffui::ConflictDecision conflictDecision{ffui::ConflictChoice::KeepBoth, true};
int conflictQuestionCount = 0;
int terminalEventCount = 0;
ffui::FileOperations* cancelOnProgress = nullptr;

LRESULT CALLBACK EventProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == ffui::WM_APP_FILE_OPERATION_CONFLICT) {
        auto* question = reinterpret_cast<ffui::FileOperationConflictQuestion*>(lParam);
        {
            std::lock_guard lock(question->mutex);
            question->decision = conflictDecision;
            question->answered = true;
            ++conflictQuestionCount;
        }
        question->answeredCondition.notify_one();
        return 0;
    }
    if (message == ffui::WM_APP_FILE_OPERATION_EVENT) {
        std::unique_ptr<ffui::FileOperationEvent> event(reinterpret_cast<ffui::FileOperationEvent*>(lParam));
        if (event->kind == ffui::FileOperationEventKind::Progress && event->total > 0 && cancelOnProgress != nullptr) {
            cancelOnProgress->CancelCurrent();
            cancelOnProgress = nullptr;
        }
        if (event->kind == ffui::FileOperationEventKind::Completed || event->kind == ffui::FileOperationEventKind::Cancelled) {
            lastEvent = std::move(event);
            ++terminalEventCount;
        }
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
std::unique_ptr<ffui::FileOperationEvent> Run(ffui::FileOperations& operations, ffui::FileOperationRequest request) {
    lastEvent.reset();
    operations.Enqueue(std::move(request));
    const ULONGLONG deadline = GetTickCount64() + 60000;
    MSG message{};
    while (!lastEvent && GetTickCount64() < deadline) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 25, QS_ALLINPUT);
    }
    Check(lastEvent != nullptr, "file operation timed out");
    return std::move(lastEvent);
}

void WriteText(const std::filesystem::path& path, const char* text) {
    std::ofstream stream(path, std::ios::binary);
    stream << text;
}

void CreateSparseFile(const std::filesystem::path& path, uint64_t bytes) {
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(handle != INVALID_HANDLE_VALUE, "could not create sparse-file fixture");
    DWORD returned = 0;
    Check(DeviceIoControl(handle, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr) != FALSE,
          "could not mark sparse file");
    LARGE_INTEGER size{};
    size.QuadPart = static_cast<LONGLONG>(bytes);
    Check(SetFilePointerEx(handle, size, nullptr, FILE_BEGIN) != FALSE && SetEndOfFile(handle) != FALSE,
          "could not size sparse file");
    CloseHandle(handle);
}

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

} // namespace

int main() {
    Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM initialization failed");
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = EventProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"FastFilesOperationTestWindow";
    Check(RegisterClassW(&windowClass) != 0, "window class registration failed");
    HWND window = CreateWindowW(windowClass.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                windowClass.hInstance, nullptr);
    Check(window != nullptr, "event window creation failed");

    wchar_t temporary[MAX_PATH]{};
    Check(GetTempPathW(MAX_PATH, temporary) != 0, "temp path unavailable");
    const auto root = std::filesystem::path(temporary) /
        (L"FastFiles-fileops-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    const auto source = root / L"source";
    const auto destination = root / L"destination";
    std::filesystem::create_directories(source);
    std::filesystem::create_directories(destination);

    ffui::FileOperations operations;
    Check(operations.Start(window), "worker did not start");

    auto event = Run(operations, {ffui::FileOperationKind::CreateFolder, {}, root.wstring(), L"created", true});
    Check(event->failures.empty() && std::filesystem::is_directory(root / L"created") &&
          event->createdPaths.size() == 1 && event->createdPaths.front() == (root / L"created").wstring(),
          "folder creation failed or did not expose the item for inline rename");
    event = Run(operations, {ffui::FileOperationKind::CreateFile, {}, (root / L"created").wstring(), L"empty.txt", true});
    Check(event->failures.empty() && std::filesystem::is_regular_file(root / L"created" / L"empty.txt") &&
          event->createdPaths.size() == 1, "file creation failed or did not expose the item for inline rename");
    event = Run(operations, {ffui::FileOperationKind::Rename, {(root / L"created" / L"empty.txt").wstring()}, {}, L"renamed.txt", true});
    Check(event->failures.empty() && std::filesystem::exists(root / L"created" / L"renamed.txt"), "rename failed");
    Check(event->reversibleOperation && event->reversibleOperation->kind == ffui::ReversibleOperationKind::Rename,
          "rename was not recorded as reversible");
    ffui::FileOperationRequest undoRename{};
    undoRename.kind = ffui::FileOperationKind::Rename;
    undoRename.sources = {event->reversibleOperation->paths.front().resultingPath};
    undoRename.newName = L"empty.txt";
    undoRename.recordHistory = false;
    event = Run(operations, std::move(undoRename));
    Check(event->failures.empty() && std::filesystem::exists(root / L"created" / L"empty.txt") && !event->reversibleOperation,
          "rename undo failed or recursively recorded history");

    WriteText(source / L"one.txt", "one");
    event = Run(operations, {ffui::FileOperationKind::Copy, {(source / L"one.txt").wstring()}, destination.wstring(), {}, true});
    Check(event->failures.empty() && std::filesystem::exists(destination / L"one.txt"), "copy failed");
    WriteText(source / L"two.txt", "two");
    event = Run(operations, {ffui::FileOperationKind::Move, {(source / L"two.txt").wstring()}, destination.wstring(), {}, true});
    Check(event->failures.empty() && !std::filesystem::exists(source / L"two.txt") && std::filesystem::exists(destination / L"two.txt"), "move failed");
    Check(event->reversibleOperation && event->reversibleOperation->kind == ffui::ReversibleOperationKind::Move,
          "move was not recorded as reversible");
    ffui::FileOperationRequest undoMove{};
    undoMove.kind = ffui::FileOperationKind::Move;
    undoMove.sources = {(destination / L"two.txt").wstring()};
    undoMove.destination = source.wstring();
    undoMove.recordHistory = false;
    event = Run(operations, std::move(undoMove));
    Check(event->failures.empty() && std::filesystem::exists(source / L"two.txt") && !event->reversibleOperation,
          "move undo failed or recursively recorded history");

    WriteText(source / L"recyclable.txt", "restore me");
    event = Run(operations, {ffui::FileOperationKind::Delete, {(source / L"recyclable.txt").wstring()}, {}, {}, true});
    Check(event->failures.empty() && !std::filesystem::exists(source / L"recyclable.txt") && event->reversibleOperation &&
          !event->reversibleOperation->paths.front().shellItemId.empty(), "Recycle Bin delete was not captured for undo");
    ffui::FileOperationRequest restore{};
    restore.kind = ffui::FileOperationKind::Restore;
    restore.sources = {(source / L"recyclable.txt").wstring()};
    restore.restorePaths = event->reversibleOperation->paths;
    restore.recordHistory = false;
    event = Run(operations, std::move(restore));
    Check(event->failures.empty() && std::filesystem::exists(source / L"recyclable.txt") && !event->reversibleOperation,
          "Recycle Bin restore failed");

    WriteText(source / L"permanent.txt", "gone");
    event = Run(operations, {ffui::FileOperationKind::Delete, {(source / L"permanent.txt").wstring()}, {}, {}, false});
    Check(event->failures.empty() && !event->reversibleOperation, "permanent delete was incorrectly recorded as undoable");

    WriteText(source / L"replace.txt", "incoming");
    WriteText(destination / L"replace.txt", "existing");
    conflictQuestionCount = 0;
    conflictDecision = {ffui::ConflictChoice::Replace, true};
    ffui::FileOperationRequest replace{};
    replace.kind = ffui::FileOperationKind::Move;
    replace.sources = {(source / L"replace.txt").wstring()};
    replace.destination = destination.wstring();
    replace.transferPlan = {{replace.sources.front(), {}, true}};
    replace.recordHistory = false;
    event = Run(operations, std::move(replace));
    Check(event->failures.empty() && conflictQuestionCount == 1 &&
              ReadText(destination / L"replace.txt") == "incoming",
          "pre-baked plan bypassed the conflict policy instead of overwriting via Replace");
    Check(!event->reversibleOperation, "overwrite was incorrectly recorded as undoable");

    // Undo-shaped requests (pre-baked plan, recordHistory=false) must honor
    // the conflict policy instead of silently overwriting an occupied target.
    WriteText(source / L"undo-move.txt", "moved away");
    event = Run(operations, {ffui::FileOperationKind::Move, {(source / L"undo-move.txt").wstring()},
                             destination.wstring(), {}, true});
    Check(event->reversibleOperation && event->reversibleOperation->kind == ffui::ReversibleOperationKind::Move,
          "undo fixture move failed");
    const auto undoPaths = event->reversibleOperation->paths;
    WriteText(source / L"undo-move.txt", "created after the move");
    conflictQuestionCount = 0;
    conflictDecision = {ffui::ConflictChoice::KeepBoth, true};
    ffui::FileOperationRequest undoMoveConflict{};
    undoMoveConflict.kind = ffui::FileOperationKind::Move;
    undoMoveConflict.sources = {undoPaths.front().resultingPath};
    undoMoveConflict.destination = source.wstring();
    undoMoveConflict.transferPlan = {{undoPaths.front().resultingPath,
                                      std::filesystem::path(undoPaths.front().originalPath).filename().wstring(), false}};
    undoMoveConflict.recordHistory = false;
    event = Run(operations, std::move(undoMoveConflict));
    Check(event->failures.empty() && conflictQuestionCount == 1 &&
              ReadText(source / L"undo-move.txt") == "created after the move" &&
              ReadText(source / L"undo-move (2).txt") == "moved away",
          "undo move silently overwrote an occupied target instead of applying conflict policy");

    // Self-target transfers must be rejected without prompting the user.
    WriteText(source / L"self.txt", "self");
    conflictQuestionCount = 0;
    conflictDecision = {ffui::ConflictChoice::Replace, true};
    event = Run(operations, {ffui::FileOperationKind::Copy, {(source / L"self.txt").wstring()}, source.wstring(), {}, true});
    Check(event->failures.size() == 1 && event->failures.front().path == (source / L"self.txt").wstring() &&
              conflictQuestionCount == 0 && ReadText(source / L"self.txt") == "self",
          "copy onto itself was not rejected as a self-target transfer");
    const auto nested = source / L"nested";
    std::filesystem::create_directories(nested);
    WriteText(nested / L"inner.txt", "inner");
    event = Run(operations, {ffui::FileOperationKind::Copy, {source.wstring()}, nested.wstring(), {}, true});
    Check(event->failures.size() == 1 && conflictQuestionCount == 0 && std::filesystem::exists(nested / L"inner.txt"),
          "copy of a folder into its own subtree was not rejected");

    const auto duplicateSourceA = root / L"duplicate-source-a";
    const auto duplicateSourceB = root / L"duplicate-source-b";
    std::filesystem::create_directories(duplicateSourceA);
    std::filesystem::create_directories(duplicateSourceB);
    WriteText(duplicateSourceA / L"same.txt", "first source");
    WriteText(duplicateSourceB / L"same.txt", "second source");
    conflictQuestionCount = 0;
    conflictDecision = {ffui::ConflictChoice::Replace, true};
    ffui::FileOperationRequest duplicateCopy{};
    duplicateCopy.kind = ffui::FileOperationKind::Copy;
    duplicateCopy.destination = destination.wstring();
    duplicateCopy.sources = {(duplicateSourceA / L"same.txt").wstring(),
                             (duplicateSourceB / L"same.txt").wstring()};
    event = Run(operations, std::move(duplicateCopy));
    Check(event->failures.empty() && conflictQuestionCount == 1 &&
              ReadText(destination / L"same.txt") == "first source" &&
              ReadText(destination / L"same (2).txt") == "second source",
          "in-batch Replace collision overwrote an earlier source");

    ffui::FileOperationRequest link{};
    link.kind = ffui::FileOperationKind::Link;
    link.sources = {(source / L"valid.txt").wstring()};
    link.destination = destination.wstring();
    link.transferPlan = {{link.sources.front(), L"valid shortcut.lnk", false}};
    event = Run(operations, std::move(link));
    Check(event->failures.empty() && std::filesystem::exists(destination / L"valid shortcut.lnk"),
          "drag link worker operation failed");

    const auto cancellationSource = root / L"cancel-source";
    const auto cancellationDestination = root / L"cancel-destination";
    std::filesystem::create_directories(cancellationSource);
    std::filesystem::create_directories(cancellationDestination);
    ffui::FileOperationRequest cancellable{};
    cancellable.kind = ffui::FileOperationKind::Copy;
    cancellable.destination = cancellationDestination.wstring();
    CreateSparseFile(cancellationSource / L"multi-gigabyte-sparse.bin", 2ull * 1024ull * 1024ull * 1024ull);
    cancellable.sources.push_back((cancellationSource / L"multi-gigabyte-sparse.bin").wstring());
    for (int index = 0; index < 2048; ++index) {
        const auto item = cancellationSource / (L"item-" + std::to_wstring(index) + L".txt");
        WriteText(item, "cancel test");
        cancellable.sources.push_back(item.wstring());
    }
    cancelOnProgress = &operations;
    event = Run(operations, std::move(cancellable));
    cancelOnProgress = nullptr;
    Check(event->kind == ffui::FileOperationEventKind::Cancelled && event->completed < event->total &&
          event->total >= 2049, "cooperative mid-batch cancellation did not stop the large batch");

    WriteText(source / L"valid.txt", "valid");
    event = Run(operations, {ffui::FileOperationKind::Copy,
        {(source / L"vanished.txt").wstring(), (source / L"valid.txt").wstring()}, destination.wstring(), {}, true});
    Check(!event->failures.empty() && std::filesystem::exists(destination / L"valid.txt"),
          "vanished item aborted remaining batch");

    WriteText(source / L"locked.txt", "locked");
    WriteText(source / L"after-locked.txt", "still copied");
    HANDLE locked = CreateFileW((source / L"locked.txt").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(locked != INVALID_HANDLE_VALUE, "could not establish exclusive lock");
    event = Run(operations, {ffui::FileOperationKind::Copy,
        {(source / L"locked.txt").wstring(), (source / L"after-locked.txt").wstring()}, destination.wstring(), {}, true});
    CloseHandle(locked);
    Check(!event->failures.empty() && std::filesystem::exists(destination / L"after-locked.txt"),
          "locked item aborted remaining batch");

    const auto deniedDestination = root / L"permission-denied";
    std::filesystem::create_directory(deniedDestination);
    std::wstring deniedPath = deniedDestination.wstring();
    PACL oldAcl = nullptr;
    PSECURITY_DESCRIPTOR securityDescriptor = nullptr;
    DWORD securityResult = GetNamedSecurityInfoW(deniedPath.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                                  nullptr, nullptr, &oldAcl, nullptr, &securityDescriptor);
    SID_IDENTIFIER_AUTHORITY worldAuthority = SECURITY_WORLD_SID_AUTHORITY;
    PSID everyone = nullptr;
    Check(securityResult == ERROR_SUCCESS && AllocateAndInitializeSid(&worldAuthority, 1, SECURITY_WORLD_RID,
          0, 0, 0, 0, 0, 0, 0, &everyone), "could not prepare permission-denied test");
    EXPLICIT_ACCESSW deniedAccess{};
    deniedAccess.grfAccessPermissions = FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY;
    deniedAccess.grfAccessMode = DENY_ACCESS;
    deniedAccess.grfInheritance = NO_INHERITANCE;
    deniedAccess.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    deniedAccess.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    deniedAccess.Trustee.ptstrName = static_cast<LPWSTR>(everyone);
    PACL deniedAcl = nullptr;
    securityResult = SetEntriesInAclW(1, &deniedAccess, oldAcl, &deniedAcl);
    if (securityResult == ERROR_SUCCESS) {
        securityResult = SetNamedSecurityInfoW(deniedPath.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                               nullptr, nullptr, deniedAcl, nullptr);
    }
    WriteText(source / L"permission.txt", "permission");
    std::unique_ptr<ffui::FileOperationEvent> permissionEvent;
    if (securityResult == ERROR_SUCCESS) {
        permissionEvent = Run(operations, {ffui::FileOperationKind::Copy,
            {(source / L"permission.txt").wstring()}, deniedDestination.wstring(), {}, true});
    }
    SetNamedSecurityInfoW(deniedPath.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                          nullptr, nullptr, oldAcl, nullptr);
    if (deniedAcl != nullptr) LocalFree(deniedAcl);
    if (everyone != nullptr) FreeSid(everyone);
    if (securityDescriptor != nullptr) LocalFree(securityDescriptor);
    Check(permissionEvent && !permissionEvent->failures.empty(), "permission-denied operation was not reported safely");

    WriteText(source / L"conflict-a.txt", "incoming-a");
    WriteText(source / L"conflict-b.txt", "incoming-b");
    WriteText(destination / L"conflict-a.txt", "existing-a");
    WriteText(destination / L"conflict-b.txt", "existing-b");
    conflictQuestionCount = 0;
    conflictDecision = {ffui::ConflictChoice::KeepBoth, true};
    ffui::FileOperationRequest keepBoth{};
    keepBoth.kind = ffui::FileOperationKind::Copy;
    keepBoth.destination = destination.wstring();
    keepBoth.sources = {(source / L"conflict-a.txt").wstring(), (source / L"conflict-b.txt").wstring()};
    event = Run(operations, std::move(keepBoth));
    Check(event->failures.empty() && conflictQuestionCount == 1 && std::filesystem::exists(destination / L"conflict-a (2).txt") &&
          std::filesystem::exists(destination / L"conflict-b (2).txt"), "Keep Both transfer failed");

    const auto skipPlan = ffui::BuildTransferPlan({(source / L"conflict-a.txt").wstring()}, destination.wstring(),
        [](const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; },
        [](const std::wstring&, const std::wstring&) { return ffui::ConflictDecision{ffui::ConflictChoice::Skip, false}; });
    Check(skipPlan && skipPlan->empty() && ReadText(destination / L"conflict-a.txt") == "existing-a",
          "Skip modified the existing destination");

    // One cancellation must stop the running operation AND every queued
    // sibling, not just the item currently executing.
    {
        const auto queuedSource = root / L"queued-source";
        const auto queuedDestination = root / L"queued-destination";
        std::filesystem::create_directories(queuedSource);
        std::filesystem::create_directories(queuedDestination);
        CreateSparseFile(queuedSource / L"big.bin", 2ull * 1024ull * 1024ull * 1024ull);
        WriteText(queuedSource / L"small.txt", "queued");
        ffui::FileOperationRequest big{};
        big.kind = ffui::FileOperationKind::Copy;
        big.destination = queuedDestination.wstring();
        big.sources = {(queuedSource / L"big.bin").wstring()};
        ffui::FileOperationRequest queuedSmall = big;
        queuedSmall.sources = {(queuedSource / L"small.txt").wstring()};
        operations.Enqueue(std::move(big));
        operations.Enqueue(std::move(queuedSmall));
        cancelOnProgress = &operations;
        const int terminalBefore = terminalEventCount;
        const ULONGLONG deadline = GetTickCount64() + 60000;
        MSG message{};
        while (terminalEventCount < terminalBefore + 2 && GetTickCount64() < deadline) {
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 25, QS_ALLINPUT);
        }
        cancelOnProgress = nullptr;
        Check(terminalEventCount >= terminalBefore + 2, "queued batch did not drain after cancellation");
        Check(!std::filesystem::exists(queuedDestination / L"small.txt"),
              "cancellation did not stop a queued-not-yet-started operation");
    }

    // Long paths (>260 chars) must still be seen by the existence preflight,
    // so the conflict policy engages instead of the shell failing silently.
    // std::filesystem cannot delete paths past MAX_PATH, so cleanup walks the
    // tree with extended-length prefixes via Win32 directly.
    std::function<void(const std::wstring&)> removeTree = [&removeTree](const std::wstring& dir) {
        WIN32_FIND_DATAW data{};
        HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &data);
        if (find != INVALID_HANDLE_VALUE) {
            do {
                const std::wstring name = data.cFileName;
                if (name == L"." || name == L"..") continue;
                const std::wstring child = dir + L"\\" + name;
                if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) removeTree(child);
                else DeleteFileW(child.c_str());
            } while (FindNextFileW(find, &data));
            FindClose(find);
        }
        RemoveDirectoryW(dir.c_str());
    };
    std::wstring longPathFixture;
    {
        std::wstring deepDirectory = L"\\\\?\\" + root.wstring();
        for (int index = 0; index < 10; ++index) {
            deepDirectory += L"\\long-path-component-" + std::to_wstring(index);
        }
        longPathFixture = deepDirectory;
        Check(std::filesystem::create_directories(deepDirectory), "long-path fixture directory not created");
        const std::wstring deepFile = deepDirectory + L"\\self.txt";
        HANDLE deepHandle = CreateFileW(deepFile.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
        Check(deepHandle != INVALID_HANDLE_VALUE, "long-path fixture file not created");
        CloseHandle(deepHandle);
        conflictQuestionCount = 0;
        conflictDecision = {ffui::ConflictChoice::Skip, false};
        ffui::FileOperationRequest longPathCopy{};
        longPathCopy.kind = ffui::FileOperationKind::Copy;
        longPathCopy.destination = deepDirectory.substr(4); // unprefixed form, past MAX_PATH
        longPathCopy.sources = {(source / L"self.txt").wstring()};
        event = Run(operations, std::move(longPathCopy));
        Check(conflictQuestionCount == 1 && ReadText(source / L"self.txt") == "self",
              "existence preflight missed an occupied destination past MAX_PATH");
        // std::filesystem cannot delete paths past MAX_PATH; walk the tree
        // with extended-length prefixes instead.
        removeTree(longPathFixture);
        Check(!std::filesystem::exists(longPathFixture.substr(4)), "long-path fixture cleanup failed");
    }

    // Stop() latches the cancel flag; a fresh Start() must clear it so later
    // operations are not instantly cancelled.
    operations.Stop();
    Check(operations.Start(window), "worker did not restart after Stop");
    event = Run(operations, {ffui::FileOperationKind::CreateFolder, {}, root.wstring(), L"after-restart", true});
    Check(event->kind == ffui::FileOperationEventKind::Completed && event->failures.empty() &&
              std::filesystem::is_directory(root / L"after-restart"),
          "operation was cancelled after Start following Stop");

    operations.Stop();
    DestroyWindow(window);
    CoUninitialize();
    removeTree(L"\\\\?\\" + root.wstring());
    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
    Check(!cleanupError, "temporary test directory cleanup failed");
    std::cout << "file operations end-to-end tests passed\n";
    return fftest::FailureCount();
}
