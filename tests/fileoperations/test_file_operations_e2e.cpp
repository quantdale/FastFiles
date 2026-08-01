#include "FileOperations.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <windows.h>
#include <objbase.h>
#include <aclapi.h>

namespace {

std::unique_ptr<ffui::FileOperationEvent> lastEvent;
ffui::ConflictDecision conflictDecision{ffui::ConflictChoice::KeepBoth, true};
int conflictQuestionCount = 0;
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
        if (event->kind == ffui::FileOperationEventKind::Progress && cancelOnProgress != nullptr) {
            cancelOnProgress->CancelCurrent();
            cancelOnProgress = nullptr;
        }
        if (event->kind == ffui::FileOperationEventKind::Completed || event->kind == ffui::FileOperationEventKind::Cancelled) {
            lastEvent = std::move(event);
        }
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void Check(bool value, const char* message) {
    if (!value) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

std::unique_ptr<ffui::FileOperationEvent> Run(ffui::FileOperations& operations, ffui::FileOperationRequest request) {
    lastEvent.reset();
    operations.Enqueue(std::move(request));
    const ULONGLONG deadline = GetTickCount64() + 15000;
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
    ffui::FileOperationRequest replace{};
    replace.kind = ffui::FileOperationKind::Move;
    replace.sources = {(source / L"replace.txt").wstring()};
    replace.destination = destination.wstring();
    replace.transferPlan = {{replace.sources.front(), {}, true}};
    event = Run(operations, std::move(replace));
    Check(event->failures.empty() && ReadText(destination / L"replace.txt") == "incoming",
          "Replace did not overwrite the destination");
    Check(!event->reversibleOperation, "overwrite was incorrectly recorded as undoable");

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
    for (int index = 0; index < 512; ++index) {
        const auto item = cancellationSource / (L"item-" + std::to_wstring(index) + L".txt");
        WriteText(item, "cancel test");
        cancellable.sources.push_back(item.wstring());
    }
    cancelOnProgress = &operations;
    event = Run(operations, std::move(cancellable));
    cancelOnProgress = nullptr;
    Check(event->kind == ffui::FileOperationEventKind::Cancelled && event->completed < event->total,
          "cooperative mid-batch cancellation did not stop remaining items");

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

    operations.Stop();
    DestroyWindow(window);
    CoUninitialize();
    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
    Check(!cleanupError, "temporary test directory cleanup failed");
    std::cout << "file operations end-to-end tests passed\n";
}
