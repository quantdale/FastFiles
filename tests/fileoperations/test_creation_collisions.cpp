#include "FileOperations.h"
#include "../TestSupport.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wrl/client.h>

namespace {

using fftest::Check;
using Microsoft::WRL::ComPtr;
std::unique_ptr<ffui::FileOperationEvent> terminal;
unsigned int terminals = 0;

LRESULT CALLBACK EventProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == ffui::WM_APP_FILE_OPERATION_EVENT) {
        std::unique_ptr<ffui::FileOperationEvent> event(reinterpret_cast<ffui::FileOperationEvent*>(lParam));
        if (event && (event->kind == ffui::FileOperationEventKind::Completed ||
                      event->kind == ffui::FileOperationEventKind::Cancelled)) {
            terminal = std::move(event);
            ++terminals;
        }
        return 0;
    }
    if (message == ffui::WM_APP_FILE_OPERATION_CONFLICT) {
        std::unique_ptr<ffui::FileOperationConflictQuestionRef> question(
            reinterpret_cast<ffui::FileOperationConflictQuestionRef*>(lParam));
        Check(false, "creation/rename probe must not request replacement consent");
        if (question && *question) {
            std::lock_guard lock((*question)->mutex);
            (*question)->decision = {ffui::ConflictChoice::Cancel, false};
            (*question)->answered = true;
            (*question)->answeredCondition.notify_one();
        }
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool WriteNew(const std::filesystem::path& path, const std::string& content) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(file != INVALID_HANDLE_VALUE, "fixture exclusively creates its own sentinel");
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(file, content.data(), static_cast<DWORD>(content.size()), &written, nullptr) != FALSE &&
                    written == content.size();
    const bool closed = CloseHandle(file) != FALSE;
    Check(ok && closed, "fixture sentinel bytes written and handle closed");
    return ok && closed;
}

std::string Read(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

bool HasExactLeaf(const std::filesystem::path& path) {
    for (const auto& item : std::filesystem::directory_iterator(path.parent_path())) {
        if (item.path().filename() == path.filename()) return true;
    }
    return false;
}

bool Run(ffui::FileOperations& operations, ffui::FileOperationRequest request) {
    terminal.reset();
    const auto before = terminals;
    operations.Enqueue(std::move(request));
    const auto deadline = GetTickCount64() + 15000;
    while ((!terminal || operations.HasOutstandingWork()) && GetTickCount64() < deadline) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
    }
    const bool done = terminal && !operations.HasOutstandingWork();
    Check(done && terminals == before + 1, "worker drains with exactly one terminal outcome");
    if (terminal) {
        std::cout << "worker completed=" << terminal->completed << " failures=" << terminal->failures.size()
                  << " createdPaths=" << terminal->createdPaths.size() << '\n';
        for (const auto& failure : terminal->failures) {
            std::cout << "error=0x" << std::hex << static_cast<unsigned long>(failure.error) << std::dec << '\n';
        }
    }
    return done;
}

// original-vision-journey-recovery 4.1: actual worker collisions, without
// racing or substituting a Shell model for application-level behavior.
void CheckWorkerCollisions(ffui::FileOperations& operations, const std::filesystem::path& root) {
    const auto target = root / L"existing.txt";
    const auto source = root / L"incoming.txt";
    if (!WriteNew(target, "existing destination sentinel") || !WriteNew(source, "incoming source sentinel")) return;
    if (Run(operations, {ffui::FileOperationKind::CreateFile, {}, root.wstring(), L"existing.txt", true})) {
        Check(Read(target) == "existing destination sentinel", "CreateFile preserves existing file bytes");
        Check(terminal->completed == 0 && !terminal->failures.empty(), "existing-file creation is not reported as new success");
        Check(terminal->createdPaths.empty(), "existing file must not be offered for new-item inline rename");
    }
    if (Run(operations, {ffui::FileOperationKind::Rename, {source.wstring()}, {}, L"existing.txt", true})) {
        Check(Read(target) == "existing destination sentinel" && Read(source) == "incoming source sentinel",
              "Rename preserves both existing destination and source");
        Check(terminal->completed == 0 && !terminal->failures.empty(), "occupied rename reports refusal");
    }
    const auto directory = root / L"existing-folder";
    Check(std::filesystem::create_directory(directory), "fixture exclusively creates existing folder");
    if (!WriteNew(directory / L"child.txt", "existing child sentinel")) return;
    if (Run(operations, {ffui::FileOperationKind::CreateFolder, {}, root.wstring(), L"existing-folder", true})) {
        Check(Read(directory / L"child.txt") == "existing child sentinel", "CreateFolder preserves an existing folder's children");
        Check(terminal->completed == 0 && !terminal->failures.empty(), "existing folder is not reported as newly created");
        Check(terminal->createdPaths.empty(), "existing folder must not be offered for new-item inline rename");
    }
    if (Run(operations, {ffui::FileOperationKind::CreateFile, {}, root.wstring(), L"fresh.txt", true})) {
        Check(terminal->completed == 1 && terminal->failures.empty() && std::filesystem::exists(root / L"fresh.txt"),
              "fresh file positive control succeeds");
        Check(terminal->createdPaths == std::vector<std::wstring>{(root / L"fresh.txt").wstring()},
              "new-file result identifies exactly the file this request created");
    }
    if (Run(operations, {ffui::FileOperationKind::CreateFolder, {}, root.wstring(), L"fresh-folder", true})) {
        Check(terminal->completed == 1 && terminal->failures.empty() && std::filesystem::is_directory(root / L"fresh-folder"),
              "fresh folder positive control succeeds after occupied-folder refusal");
        Check(terminal->createdPaths == std::vector<std::wstring>{(root / L"fresh-folder").wstring()},
              "new-folder result identifies exactly the directory this request created");
    }
    for (const auto kind : {ffui::FileOperationKind::CreateFile, ffui::FileOperationKind::CreateFolder}) {
        if (Run(operations, {kind, {}, root.wstring(), L"../escaped.txt", true})) {
            Check(terminal->completed == 0 && !terminal->failures.empty() && terminal->createdPaths.empty(),
                  "invalid creation leaf cannot escape its destination or publish a created path");
        }
        const auto otherName = kind == ffui::FileOperationKind::CreateFile ? L"existing-folder" : L"existing.txt";
        if (Run(operations, {kind, {}, root.wstring(), otherName, true})) {
            Check(terminal->completed == 0 && !terminal->failures.empty() && terminal->createdPaths.empty(),
                  "creation refuses an existing destination of the other type");
        }
    }
    const auto renamedDirectory = root / L"renamed-folder";
    if (Run(operations, {ffui::FileOperationKind::Rename, {directory.wstring()}, {}, L"renamed-folder", true})) {
        Check(terminal->completed == 1 && terminal->failures.empty() &&
              Read(renamedDirectory / L"child.txt") == "existing child sentinel",
              "directory rename preserves its child bytes");
        Check(terminal->reversibleOperation && terminal->reversibleOperation->paths.size() == 1 &&
              terminal->reversibleOperation->paths[0].resultingPath == renamedDirectory.wstring(),
              "directory rename retains its exact result history");
    }
    if (Run(operations, {ffui::FileOperationKind::Rename, {renamedDirectory.wstring()}, {}, L"RENAMED-FOLDER", true})) {
        Check(terminal->completed == 1 && terminal->failures.empty() && HasExactLeaf(root / L"RENAMED-FOLDER") &&
              Read(root / L"RENAMED-FOLDER" / L"child.txt") == "existing child sentinel",
              "case-only directory rename updates actual leaf casing and preserves children");
    }
    if (Run(operations, {ffui::FileOperationKind::Rename, {(root / L"RENAMED-FOLDER").wstring()}, {}, L"existing-folder", true})) {
        Check(terminal->completed == 1 && terminal->failures.empty(), "directory rename history's reverse path succeeds");
    }
    if (Run(operations, {ffui::FileOperationKind::Rename, {source.wstring()}, {}, L"../invalid.txt", true})) {
        Check(terminal->completed == 0 && !terminal->failures.empty() && !terminal->reversibleOperation &&
              Read(source) == "incoming source sentinel", "invalid rename leaf preserves source without history");
    }
    const auto renamed = root / L"INCOMING.txt";
    if (Run(operations, {ffui::FileOperationKind::Rename, {source.wstring()}, {}, L"INCOMING.txt", true})) {
        Check(terminal->completed == 1 && terminal->failures.empty() && HasExactLeaf(renamed) &&
              Read(renamed) == "incoming source sentinel",
              "case-only rename succeeds without replacing another item");
        Check(terminal->reversibleOperation && terminal->reversibleOperation->paths.size() == 1 &&
              terminal->reversibleOperation->paths[0].originalPath == source.wstring() &&
              terminal->reversibleOperation->paths[0].resultingPath == renamed.wstring(),
              "case-only rename retains exact source and result history");
    }
    if (Run(operations, {ffui::FileOperationKind::Rename, {renamed.wstring()}, {}, L"recovered.txt", true})) {
        Check(terminal->completed == 1 && terminal->failures.empty() && Read(root / L"recovered.txt") == "incoming source sentinel",
              "fresh exact-name rename succeeds after collision refusal");
    }
    ffui::FileOperationRequest noHistory{ffui::FileOperationKind::Rename, {(root / L"recovered.txt").wstring()}, {}, L"final.txt", true};
    noHistory.recordHistory = false;
    if (Run(operations, std::move(noHistory))) {
        Check(terminal->completed == 1 && terminal->failures.empty() && !terminal->reversibleOperation,
              "undo-request rename does not create a new history entry");
    }
    Check(Read(target) == "existing destination sentinel" && Read(directory / L"child.txt") == "existing child sentinel",
          "all later requests preserve both original sentinels");
}

void CheckLateRename(const std::filesystem::path& root) {
    for (const bool folder : {false, true}) {
        const auto scope = root / (folder ? L"native-late-folder" : L"native-late-file");
        Check(std::filesystem::create_directory(scope), "late-rename probe owns scope exclusively");
        const auto source = scope / L"source";
        const auto destination = scope / L"destination";
        if (folder) {
            Check(std::filesystem::create_directory(source), "late directory source created");
            if (!WriteNew(source / L"child.txt", "incoming child sentinel")) return;
        } else if (!WriteNew(source, "incoming file sentinel")) return;
        Check(!std::filesystem::exists(destination), "rename target is absent at validation");
        if (folder) {
            Check(std::filesystem::create_directory(destination), "late target directory created after validation");
            if (!WriteNew(destination / L"child.txt", "late child sentinel")) return;
        } else if (!WriteNew(destination, "late file sentinel")) return;
        const HRESULT result = ffui::RenameWithoutReplacement(source.wstring(), destination.wstring());
        Check(FAILED(result), "production rename primitive refuses a late occupied destination");
        Check(Read(folder ? source / L"child.txt" : source) == (folder ? "incoming child sentinel" : "incoming file sentinel"),
              "late-collision refusal preserves incoming source bytes");
        Check(Read(folder ? destination / L"child.txt" : destination) == (folder ? "late child sentinel" : "late file sentinel"),
              "late-collision refusal preserves the arriving destination bytes");
    }
}

// Shell-boundary probe only: queue the same native operation and flags as
// FileOperations, then CREATE_NEW a destination before PerformOperations.
// This observes Shell's late-collision behavior deterministically; it does
// NOT claim to inject this boundary inside the real worker's Rename precheck.
void CheckQueuedCollision(const std::filesystem::path& root, bool rename) {
    const auto directory = root / (rename ? L"queued-rename" : L"queued-new-file");
    Check(std::filesystem::create_directory(directory), "queue probe exclusively owns its directory");
    const auto sourcePath = directory / L"incoming.txt";
    const auto target = directory / L"target.txt";
    if (rename && !WriteNew(sourcePath, "queued incoming sentinel")) return;
    ComPtr<IFileOperation> operation;
    HRESULT result = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&operation));
    Check(SUCCEEDED(result), "native Shell operation created");
    if (FAILED(result)) return;
    result = operation->SetOperationFlags(FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOERRORUI);
    ComPtr<IShellItem> item;
    if (SUCCEEDED(result)) {
        result = SHCreateItemFromParsingName((rename ? sourcePath : directory).c_str(), nullptr, IID_PPV_ARGS(&item));
    }
    if (SUCCEEDED(result)) {
        result = rename ? operation->RenameItem(item.Get(), L"target.txt", nullptr)
                        : operation->NewItem(item.Get(), FILE_ATTRIBUTE_NORMAL, L"target.txt", nullptr, nullptr);
    }
    Check(SUCCEEDED(result), "operation queued while target is absent");
    if (FAILED(result) || !WriteNew(target, "late destination sentinel")) return;
    result = operation->PerformOperations();
    BOOL aborted = FALSE;
    const HRESULT abortedResult = operation->GetAnyOperationsAborted(&aborted);
    std::cout << (rename ? "queued rename" : "queued NewItem") << " result=0x" << std::hex
              << static_cast<unsigned long>(result) << std::dec << " aborted=" << aborted << '\n';
    Check(SUCCEEDED(abortedResult), "Shell abort-state query succeeds");
    Check(Read(target) == "late destination sentinel", "late Shell destination bytes are preserved");
    if (rename) Check(Read(sourcePath) == "queued incoming sentinel", "late Shell rename preserves its incoming source");
}

} // namespace

int main(int argc, char** argv) {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Check(SUCCEEDED(initialized), "COM apartment initialized");
    if (FAILED(initialized)) return fftest::FailureCount();
    wchar_t temporary[MAX_PATH]{};
    const DWORD length = GetTempPathW(MAX_PATH, temporary);
    Check(length > 0 && length < MAX_PATH, "temporary parent available without truncation");
    if (length == 0 || length >= MAX_PATH) { CoUninitialize(); return fftest::FailureCount(); }
    const auto root = std::filesystem::path(temporary) /
        (L"FastFiles-creation-collision-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    const bool owned = std::filesystem::create_directory(root);
    Check(owned, "fixture root exclusively created; no pre-existing data accepted");
    if (!owned) { CoUninitialize(); return fftest::FailureCount(); }
    WNDCLASSW cls{};
    cls.lpfnWndProc = EventProc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"FastFilesCreationCollisionTest";
    const ATOM registered = RegisterClassW(&cls);
    Check(registered != 0, "message-only fixture class registered");
    const HWND window = registered ? CreateWindowW(cls.lpszClassName, L"", 0, 0, 0, 0, 0,
        HWND_MESSAGE, nullptr, cls.hInstance, nullptr) : nullptr;
    Check(window != nullptr, "message-only event window created");
    if (window) {
        ffui::FileOperations operations;
        const bool started = operations.Start(window);
        Check(started, "actual worker starts");
        if (started) CheckWorkerCollisions(operations, root);
        operations.Stop();
        DestroyWindow(window);
    }
    if (argc == 2 && std::string_view(argv[1]) == "--observe-legacy-shell") {
        CheckQueuedCollision(root, false);
        CheckQueuedCollision(root, true);
    } else {
        CheckLateRename(root);
    }
    if (registered) UnregisterClassW(cls.lpszClassName, cls.hInstance);
    CoUninitialize();
    std::error_code error;
    std::filesystem::remove_all(root, error);
    Check(!error && !std::filesystem::exists(root), "only exclusively created fixture tree cleaned");
    return fftest::FailureCount();
}
