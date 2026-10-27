#include "FileOperations.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <windows.h>
#include <winioctl.h>
#include <objbase.h>
#include <aclapi.h>
#include <sherrors.h>
#include <shlobj.h>
#include <deque>
#include "../TestSupport.h"

using namespace fftest;

namespace {

std::unique_ptr<ffui::FileOperationEvent> lastEvent;
ffui::ConflictDecision conflictDecision{ffui::ConflictChoice::KeepBoth, true};
int conflictQuestionCount = 0;
std::function<ffui::ConflictDecision(const ffui::FileOperationConflictQuestion&)> conflictObserver;
std::deque<ffui::ConflictDecision> conflictDecisions;
int terminalEventCount = 0;
ffui::FileOperations* cancelOnProgress = nullptr;
unsigned int minimumCancelCompleted = 0;
// Simulates the real UI's conflict dialog: when set, the posted question is
// held (unanswered) so the test can run Stop() while the prompt is in flight.
bool holdConflictQuestion = false;
std::shared_ptr<ffui::FileOperationConflictQuestion> heldQuestion;

LRESULT CALLBACK EventProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == ffui::WM_APP_FILE_OPERATION_CONFLICT) {
        // Ownership protocol: the worker posts a heap shared_ptr reference.
        std::unique_ptr<ffui::FileOperationConflictQuestionRef> posted(
            reinterpret_cast<ffui::FileOperationConflictQuestionRef*>(lParam));
        if (!posted || !*posted) return 0;
        auto question = *posted;
        if (holdConflictQuestion) {
            heldQuestion = question;
            return 0;
        }
        const auto observedDecision = conflictObserver ? std::optional{conflictObserver(*question)} : std::nullopt;
        {
            std::lock_guard lock(question->mutex);
            question->decision = observedDecision ? *observedDecision : (conflictDecisions.empty() ? conflictDecision : conflictDecisions.front());
            if (!conflictDecisions.empty()) conflictDecisions.pop_front();
            question->answered = true;
            ++conflictQuestionCount;
        }
        question->answeredCondition.notify_one();
        return 0;
    }
    if (message == ffui::WM_APP_FILE_OPERATION_EVENT) {
        std::unique_ptr<ffui::FileOperationEvent> event(reinterpret_cast<ffui::FileOperationEvent*>(lParam));
        if (event->kind == ffui::FileOperationEventKind::Progress && event->total > 0 && cancelOnProgress != nullptr && event->completed >= minimumCancelCompleted) {
            Check(cancelOnProgress->HasOutstandingWork(), "real cancellable progress lost outstanding work");
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
    while ((!lastEvent || operations.HasOutstandingWork()) && GetTickCount64() < deadline) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 25, QS_ALLINPUT);
    }
    Check(lastEvent != nullptr, "file operation timed out");
    Check(!operations.HasOutstandingWork(), "terminal operation did not drain outstanding work");
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

ffui::ReversiblePath FixtureRestorePath(const std::filesystem::path& holding, const std::filesystem::path& original) {
    PIDLIST_ABSOLUTE identity = nullptr;
    const HRESULT result = SHParseDisplayName(holding.c_str(), nullptr, &identity, 0, nullptr);
    Check(SUCCEEDED(result) && identity != nullptr, "owned Shell restore identity unavailable");
    ffui::ReversiblePath path{original.wstring(), {}, {}};
    if (identity != nullptr) {
        const auto* first = reinterpret_cast<const std::byte*>(identity);
        path.shellItemId.assign(first, first + ILGetSize(identity));
        CoTaskMemFree(identity);
    }
    return path;
}

void CheckPostPreflightCollision(ffui::FileOperations& operations, const std::filesystem::path& root, bool appeared = true, ffui::FileOperationKind kind = ffui::FileOperationKind::Copy) {
    const auto scope = root / (std::to_wstring(static_cast<int>(kind)) + (appeared ? L"-appeared" : L"-absent"));
    const auto source = scope / L"source";
    const auto target = scope / L"target";
    std::filesystem::create_directories(source);
    std::filesystem::create_directories(target);
    WriteText(source / L"first.txt", "incoming first");
    WriteText(source / L"second.txt", "incoming second");
    WriteText(target / L"second.txt", "existing second");
    WriteText(target / L"sentinel.txt", "unrelated sentinel");
    Check(!std::filesystem::exists(target / L"first.txt"), "first target initially absent");
    bool created = false;
    conflictQuestionCount = 0;
    const int beforeTerminal = terminalEventCount;
    conflictObserver = [&](const ffui::FileOperationConflictQuestion& question) {
        const auto expectedSource = kind == ffui::FileOperationKind::Restore ? target : source;
        const bool exact = question.source == (expectedSource / L"second.txt").wstring() &&
            question.destination == (target / L"second.txt").wstring();
        Check(exact, "only the expected second collision prompts");
        if (!exact) return ffui::ConflictDecision{ffui::ConflictChoice::Cancel, false};
        if (!appeared) return ffui::ConflictDecision{ffui::ConflictChoice::Skip, false};
        HANDLE file = CreateFileW((target / L"first.txt").c_str(), GENERIC_WRITE, 0, nullptr,
                                  CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        Check(file != INVALID_HANDLE_VALUE, "CREATE_NEW proves target appeared after absence preflight");
        if (file == INVALID_HANDLE_VALUE) return ffui::ConflictDecision{ffui::ConflictChoice::Cancel, false};
        constexpr char bytes[] = "new destination sentinel";
        DWORD written = 0;
        created = WriteFile(file, bytes, sizeof(bytes) - 1, &written, nullptr) != FALSE && written == sizeof(bytes) - 1;
        Check(CloseHandle(file) != FALSE, "close owned target sentinel");
        Check(created, "write exact owned sentinel before answering conflict");
        return ffui::ConflictDecision{created ? ffui::ConflictChoice::Skip : ffui::ConflictChoice::Cancel, false};
    };
    ffui::FileOperationRequest request{kind,
        {(source / L"first.txt").wstring(), (source / L"second.txt").wstring()}, target.wstring(), {}, true};
    if (kind == ffui::FileOperationKind::Restore) {
        request.restorePaths = {FixtureRestorePath(source / L"first.txt", target / L"first.txt"),
                                FixtureRestorePath(source / L"second.txt", target / L"second.txt")};
    }
    auto event = Run(operations, std::move(request));
    conflictObserver = {};
    Check((created || !appeared) && conflictQuestionCount == 1 && terminalEventCount == beforeTerminal + 1,
          "deterministic preflight boundary and exactly one terminal event");
    Check((kind == ffui::FileOperationKind::Copy ? ReadText(source / L"first.txt") == "incoming first"
                                               : !std::filesystem::exists(source / L"first.txt")) &&
          ReadText(source / L"second.txt") == "incoming second", "source outcomes match actual transfer and intentional skip");
    Check(ReadText(target / L"first.txt") == (appeared ? "new destination sentinel" : "incoming first"),
          "post-preflight destination must never be overwritten without consent");
    Check(ReadText(target / L"second.txt") == "existing second" && ReadText(target / L"sentinel.txt") == "unrelated sentinel",
          "skipped destination and unrelated sentinel preserved");
    std::filesystem::path transferred;
    size_t destinationFiles = 0;
    for (const auto& entry : std::filesystem::directory_iterator(target)) {
        ++destinationFiles;
        if (ReadText(entry.path()) == "incoming first") transferred = entry.path();
    }
    Check(!transferred.empty() && destinationFiles == (appeared ? 4u : 3u),
          "exact target inventory retains incoming copy and all existing files");
    Check(event && event->total == 2 && event->skipped == 1 && event->completed == 1 && event->failures.empty() &&
          event->kind == ffui::FileOperationEventKind::Completed, "original total, completion and intentional skip remain truthful");
    if (kind == ffui::FileOperationKind::Move) {
        Check(event && event->reversibleOperation && event->reversibleOperation->paths.size() == 1 &&
              event->reversibleOperation->paths[0].originalPath == (source / L"first.txt").wstring() &&
              event->reversibleOperation->paths[0].resultingPath == transferred.wstring(),
              "move undo names actual collision-safe destination");
    }
    if (event) std::cout << "consent diagnostic: completed=" << event->completed << " failed=" << event->failures.size()
                        << " skipped=" << event->skipped << " terminal=" << static_cast<int>(event->kind) << '\n';
}

void CheckDirectoryPostPreflightCollision(ffui::FileOperations& operations, const std::filesystem::path& root,
                                        ffui::FileOperationKind kind) {
    const auto scope = root / (L"directory-consent-" + std::to_wstring(static_cast<int>(kind)));
    const auto source = scope / L"source";
    const auto target = scope / L"target";
    std::filesystem::create_directories(source / L"Folder");
    std::filesystem::create_directories(target);
    WriteText(source / L"Folder" / L"incoming.txt", "incoming nested bytes");
    WriteText(source / L"second.txt", "incoming second");
    WriteText(target / L"second.txt", "existing second");
    conflictQuestionCount = 0;
    bool created = false;
    conflictObserver = [&](const ffui::FileOperationConflictQuestion& question) {
        const bool exact = question.source == (source / L"second.txt").wstring() &&
            question.destination == (target / L"second.txt").wstring();
        Check(exact, "directory test reaches only expected later conflict");
        if (!exact) return ffui::ConflictDecision{ffui::ConflictChoice::Cancel, false};
        created = CreateDirectoryW((target / L"Folder").c_str(), nullptr) != FALSE;
        Check(created, "new target directory appears after parent preflight");
        if (!created) return ffui::ConflictDecision{ffui::ConflictChoice::Cancel, false};
        WriteText(target / L"Folder" / L"incoming.txt", "existing nested sentinel");
        Check(ReadText(target / L"Folder" / L"incoming.txt") == "existing nested sentinel", "new directory sentinel written");
        return ffui::ConflictDecision{ffui::ConflictChoice::Skip, false};
    };
    auto event = Run(operations, {kind, {(source / L"Folder").wstring(), (source / L"second.txt").wstring()}, target.wstring(), {}, true});
    conflictObserver = {};
    Check(created && conflictQuestionCount == 1 && event && event->failures.empty(), "directory transfer finishes without repeated prompt or failure");
    Check(event && event->completed == 1 && event->total == 2 && event->skipped == 1,
          "directory child callbacks cannot inflate submitted-root completion counts");
    Check(ReadText(target / L"Folder" / L"incoming.txt") == "existing nested sentinel",
          "late directory must not merge over existing nested data");
    size_t retainedCopies = 0;
    size_t targetEntries = 0;
    std::filesystem::path transferredFolder;
    for (const auto& entry : std::filesystem::directory_iterator(target)) {
        ++targetEntries;
        if (entry.is_directory() && ReadText(entry.path() / L"incoming.txt") == "incoming nested bytes") {
            ++retainedCopies;
            transferredFolder = entry.path();
        }
    }
    Check(retainedCopies == 1 && targetEntries == 3, "exact directory inventory keeps both roots with original nested bytes");
    if (kind == ffui::FileOperationKind::Move) {
        Check(event && event->reversibleOperation && event->reversibleOperation->paths.size() == 1 &&
              event->reversibleOperation->paths[0].originalPath == (source / L"Folder").wstring() &&
              event->reversibleOperation->paths[0].resultingPath == transferredFolder.wstring(),
              "directory Move history contains only actual submitted root");
    }
    Check(ReadText(source / L"second.txt") == "incoming second" && ReadText(target / L"second.txt") == "existing second",
          "directory operation preserves skipped source and destination");
    Check(kind == ffui::FileOperationKind::Copy ? ReadText(source / L"Folder" / L"incoming.txt") == "incoming nested bytes"
                                              : !std::filesystem::exists(source / L"Folder"), "directory source follows copy or move semantics");
    if (event) std::cout << "directory consent: completed=" << event->completed << " total=" << event->total
                        << " skipped=" << event->skipped << " failures=" << event->failures.size() << '\n';
}

void CheckMixedConsentCancellation(ffui::FileOperations& operations, const std::filesystem::path& root) {
    const auto scope = root / L"mixed-cancel";
    const auto source = scope / L"source";
    const auto target = scope / L"target";
    std::filesystem::create_directories(source);
    std::filesystem::create_directories(target);
    WriteText(source / L"ordinary.txt", "ordinary incoming");
    WriteText(source / L"replace.txt", "replace incoming");
    WriteText(target / L"replace.txt", "replace original");
    conflictQuestionCount = 0;
    conflictObserver = [&](const ffui::FileOperationConflictQuestion& question) {
        const bool exact = question.source == (source / L"replace.txt").wstring() &&
            question.destination == (target / L"replace.txt").wstring();
        Check(exact, "mixed cancel binds actual replacement prompt");
        operations.CancelCurrent();
        return ffui::ConflictDecision{exact ? ffui::ConflictChoice::Replace : ffui::ConflictChoice::Cancel, false};
    };
    auto event = Run(operations, {ffui::FileOperationKind::Move,
        {(source / L"ordinary.txt").wstring(), (source / L"replace.txt").wstring()}, target.wstring(), {}, true});
    conflictObserver = {};
    Check(event && event->kind == ffui::FileOperationEventKind::Cancelled && event->completed == 0 &&
          !event->reversibleOperation && conflictQuestionCount == 1, "cancelled mixed request cannot publish completed work or undo");
    Check(ReadText(source / L"ordinary.txt") == "ordinary incoming" && ReadText(source / L"replace.txt") == "replace incoming" &&
          ReadText(target / L"replace.txt") == "replace original" && !std::filesystem::exists(target / L"ordinary.txt"),
          "cancellation prevents ordinary and approved replacement mutation");
}

void CheckInFlightMixedCancellation(ffui::FileOperations& operations, const std::filesystem::path& root) {
    const auto source = root / L"mixed-in-flight-source";
    const auto target = root / L"mixed-in-flight-target";
    std::filesystem::create_directories(source);
    std::filesystem::create_directories(target);
    ffui::FileOperationRequest request{ffui::FileOperationKind::Copy, {}, target.wstring(), {}, true};
    const std::string payload(4096, 'x');
    constexpr unsigned int count = 512;
    for (unsigned int index = 0; index < count; ++index) {
        const auto path = source / (L"item-" + std::to_wstring(index) + L".bin");
        WriteText(path, payload.c_str());
        request.sources.push_back(path.wstring());
    }
    WriteText(source / L"replace.txt", "approved incoming");
    WriteText(target / L"replace.txt", "approved existing sentinel");
    request.sources.push_back((source / L"replace.txt").wstring());
    conflictDecision = {ffui::ConflictChoice::Replace, true};
    conflictQuestionCount = 0;
    minimumCancelCompleted = 1;
    cancelOnProgress = &operations;
    auto event = Run(operations, std::move(request));
    const bool observed = cancelOnProgress == nullptr;
    if (event) {
        for (const auto& failure : event->failures) {
            std::cout << "mixed cancelled HRESULT=" << std::hex << static_cast<unsigned long>(failure.error) << std::dec << '\n';
        }
        Check(event->failures.empty(), "user-requested mixed cancellation does not invent an IO failure");
    }
    cancelOnProgress = nullptr;
    minimumCancelCompleted = 0;
    Check(observed && conflictQuestionCount == 1 && event && event->kind == ffui::FileOperationEventKind::Cancelled &&
          event->completed > 0 && event->completed < count, "mixed cancellation happens after actual ordinary transfer and before it finishes");
    Check(ReadText(target / L"replace.txt") == "approved existing sentinel" && ReadText(source / L"replace.txt") == "approved incoming",
          "accepted in-flight cancellation prevents later approved replacement phase");
    size_t copied = 0;
    for (unsigned int index = 0; index < count; ++index) {
        const auto name = L"item-" + std::to_wstring(index) + L".bin";
        Check(ReadText(source / name) == payload, "mixed cancellation retains every source payload");
        if (std::filesystem::exists(target / name)) {
            ++copied;
            Check(ReadText(target / name) == payload, "mixed cancellation retains exact completed payload bytes");
        }
    }
    Check(copied > 0 && copied < count && event && copied == event->completed,
          "mixed cancellation terminal count matches exact partial destination inventory");
    const auto targetEntries = std::distance(std::filesystem::directory_iterator(target), std::filesystem::directory_iterator{});
    Check(targetEntries == static_cast<decltype(targetEntries)>(copied + 1), "mixed cancellation has no unexpected destination names");
    std::cout << "mixed cancellation copied=" << copied << " of " << count << '\n';
}

void CheckRestoreConflictBatches(ffui::FileOperations& operations, const std::filesystem::path& root) {
    // Real worker/Shell MoveItem callbacks with fixture-bound identities, not
    // the user's Recycle Bin. Native app Undo qualification remains separate.
    for (const auto choice : {ffui::ConflictChoice::Replace, ffui::ConflictChoice::Skip, ffui::ConflictChoice::KeepBoth}) {
        const auto scope = root / (L"restore-policy-" + std::to_wstring(static_cast<int>(choice)));
        const auto holding = scope / L"holding";
        const auto restored = scope / L"restored";
        std::filesystem::create_directories(holding);
        std::filesystem::create_directories(restored);
        ffui::FileOperationRequest request{};
        request.kind = ffui::FileOperationKind::Restore;
        request.recordHistory = false;
        for (int index = 0; index < 4; ++index) {
            const auto name = L"restore-" + std::to_wstring(index) + L".txt";
            WriteText(holding / name, ("incoming-" + std::to_string(index)).c_str());
            if (index < 3) WriteText(restored / name, ("existing-" + std::to_string(index)).c_str());
            request.sources.push_back((restored / name).wstring());
            request.restorePaths.push_back(FixtureRestorePath(holding / name, restored / name));
        }
        WriteText(restored / L"restore-0 (2).txt", "suffix sentinel");
        WriteText(restored / L"sentinel.txt", "unrelated sentinel");
        conflictDecision = {choice, true};
        conflictQuestionCount = 0;
        auto event = Run(operations, request);
        Check(event && event->kind == ffui::FileOperationEventKind::Completed && event->failures.empty() &&
              event->total == 4 && event->completed == (choice == ffui::ConflictChoice::Skip ? 1u : 4u) &&
              event->skipped == (choice == ffui::ConflictChoice::Skip ? 3u : 0u) && conflictQuestionCount == 1,
              "restore apply-all must prompt once and preserve total/completed/skipped counts without failures");
        for (int index = 0; index < 4; ++index) {
            const auto name = L"restore-" + std::to_wstring(index) + L".txt";
            const bool collision = index < 3;
            const bool skipped = collision && choice == ffui::ConflictChoice::Skip;
            Check(std::filesystem::exists(holding / name) == skipped, "restore retained/lost the wrong holding item");
            const auto expected = collision && choice != ffui::ConflictChoice::Replace
                ? "existing-" + std::to_string(index) : "incoming-" + std::to_string(index);
            Check(ReadText(restored / name) == expected, "restore collision outcome changed wrong bytes");
            if (collision && choice == ffui::ConflictChoice::KeepBoth) {
                const auto duplicate = L"restore-" + std::to_wstring(index) + (index == 0 ? L" (3).txt" : L" (2).txt");
                Check(ReadText(restored / duplicate) == "incoming-" + std::to_string(index), "restore keep-both suffix or bytes wrong");
            }
        }
        Check(ReadText(restored / L"restore-0 (2).txt") == "suffix sentinel" &&
              ReadText(restored / L"sentinel.txt") == "unrelated sentinel", "restore changed unrelated/suffix sentinel");
        if (choice == ffui::ConflictChoice::Skip) {
            request.restorePaths.resize(3);
            request.sources.clear();
            conflictQuestionCount = 0;
            event = Run(operations, request);
            Check(event && event->kind == ffui::FileOperationEventKind::Completed && event->total == 3 &&
                  event->completed == 0 && event->skipped == 3 && event->failures.empty() && conflictQuestionCount == 1,
                  "all-skipped restore must report all original items without errors or Shell execution");
        }
        // The same worker must ask again on the next request; preflight Cancel
        // leaves both its colliding and noncolliding entries unmoved.
        const auto resetHolding = scope / L"reset-holding";
        std::filesystem::create_directories(resetHolding);
        WriteText(resetHolding / L"reset.txt", "reset incoming");
        WriteText(resetHolding / L"fresh.txt", "fresh incoming");
        WriteText(restored / L"reset.txt", "reset existing");
        ffui::FileOperationRequest reset{};
        reset.kind = ffui::FileOperationKind::Restore;
        reset.recordHistory = false;
        reset.restorePaths = {FixtureRestorePath(resetHolding / L"fresh.txt", restored / L"fresh.txt"),
            FixtureRestorePath(resetHolding / L"reset.txt", restored / L"reset.txt")};
        conflictDecision = {ffui::ConflictChoice::Cancel, false};
        conflictQuestionCount = 0;
        event = Run(operations, std::move(reset));
        Check(event && event->kind == ffui::FileOperationEventKind::Cancelled && event->total == 2 &&
              conflictQuestionCount == 1 && ReadText(resetHolding / L"fresh.txt") == "fresh incoming" &&
              ReadText(resetHolding / L"reset.txt") == "reset incoming" && ReadText(restored / L"reset.txt") == "reset existing" &&
              !std::filesystem::exists(restored / L"fresh.txt"), "restore decision leaked or Cancel mutated preflight items");
    }
    const auto holding = root / L"restore-unchecked-holding";
    const auto restored = root / L"restore-unchecked-target";
    std::filesystem::create_directories(holding);
    std::filesystem::create_directories(restored);
    ffui::FileOperationRequest unchecked{};
    unchecked.kind = ffui::FileOperationKind::Restore;
    unchecked.recordHistory = false;
    for (const auto* name : {L"first.txt", L"fresh.txt", L"second.txt"}) {
        WriteText(holding / name, "incoming preserved");
        if (std::wstring_view(name) != L"fresh.txt") WriteText(restored / name, "existing preserved");
        unchecked.restorePaths.push_back(FixtureRestorePath(holding / name, restored / name));
    }
    conflictQuestionCount = 0;
    conflictDecisions = {{ffui::ConflictChoice::Skip, false}, {ffui::ConflictChoice::Cancel, false}};
    auto event = Run(operations, std::move(unchecked));
    Check(event && event->kind == ffui::FileOperationEventKind::Cancelled && event->total == 3 &&
          conflictQuestionCount == 2 && conflictDecisions.empty(), "unchecked restore Skip must prompt again before later Cancel");
    for (const auto* name : {L"first.txt", L"fresh.txt", L"second.txt"}) {
        Check(ReadText(holding / name) == "incoming preserved", "cancelled restore preflight moved a source");
    }
    Check(ReadText(restored / L"first.txt") == "existing preserved" && ReadText(restored / L"second.txt") == "existing preserved" &&
          !std::filesystem::exists(restored / L"fresh.txt"), "later restore Cancel mutated an earlier planned target");
    conflictDecision = {ffui::ConflictChoice::KeepBoth, true};
}

} // namespace

int main(int argc, char** argv) {
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

    {
        ffui::FileOperations queued;
        Check(!queued.HasOutstandingWork(), "new worker must be idle");
        queued.Enqueue({ffui::FileOperationKind::CreateFolder, {}, root.wstring(), L"not-started", true});
        Check(queued.HasOutstandingWork(), "queued work must be outstanding before worker Start");
    }
    ffui::FileOperations operations;
    Check(!operations.HasOutstandingWork(), "idle operation queue must be unavailable for cancellation");
    Check(operations.Start(window), "worker did not start");
    if (argc == 2 && std::string_view(argv[1]) == "--post-preflight-consent") {
        for (const auto kind : {ffui::FileOperationKind::Copy, ffui::FileOperationKind::Move, ffui::FileOperationKind::Restore}) {
            CheckPostPreflightCollision(operations, root, true, kind);
            CheckPostPreflightCollision(operations, root, false, kind);
        }
        CheckDirectoryPostPreflightCollision(operations, root, ffui::FileOperationKind::Copy);
        CheckDirectoryPostPreflightCollision(operations, root, ffui::FileOperationKind::Move);
        CheckMixedConsentCancellation(operations, root);
        CheckInFlightMixedCancellation(operations, root);
        operations.Stop();
        DestroyWindow(window);
        CoUninitialize();
        std::error_code cleanupError;
        // root is the unique directory constructed above beneath GetTempPath;
        // this explicit diagnostic never uses the user's Recycle Bin.
        Check(std::filesystem::equivalent(root.parent_path(), std::filesystem::path(temporary)), "cleanup stays within own temporary parent");
        if (std::filesystem::equivalent(root.parent_path(), std::filesystem::path(temporary))) std::filesystem::remove_all(root, cleanupError);
        Check(!cleanupError && !std::filesystem::exists(root), "owned consent fixture cleaned normally");
        return fftest::FailureCount();
    }


    // original-vision-journey-recovery 4.6: a stalled consumer must not
    // accumulate one UI message per file callback or lose terminal outcomes
    // when its own queue reaches the Windows posted-message quota.
    {
        const auto burstStart = std::chrono::steady_clock::now();
        operations.PostEvent({ffui::FileOperationEventKind::Queued, ffui::FileOperationKind::Copy, {}, 0, 0, 0, {}});
        operations.PostEvent({ffui::FileOperationEventKind::Started, ffui::FileOperationKind::Copy, {}, 0, 100, 0, {}});
        operations.PostEvent({ffui::FileOperationEventKind::Progress, ffui::FileOperationKind::Copy, {}, 0, 0, 0, {}});
        operations.PostEvent({ffui::FileOperationEventKind::Progress, ffui::FileOperationKind::Copy, {}, 1, 100, 1, {}});
        for (unsigned int index = 0; index < 40000; ++index) {
            if (index == 20000) {
                operations.PostEvent({ffui::FileOperationEventKind::Queued, ffui::FileOperationKind::Move, {}, 0, 0, 0, {}});
            }
            operations.PostEvent({ffui::FileOperationEventKind::Progress, ffui::FileOperationKind::Copy, {}, 50, 100, 50, {}});
        }
        const auto burstElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - burstStart);
        operations.PostEvent({ffui::FileOperationEventKind::Completed, ffui::FileOperationKind::Copy,
                              L"burst-finish", 99, 100, 99, {{L"burst-failure", E_ACCESSDENIED}}});
        operations.PostEvent({ffui::FileOperationEventKind::Started, ffui::FileOperationKind::Move, {}, 0, 1, 0, {}});
        operations.PostEvent({ffui::FileOperationEventKind::Progress, ffui::FileOperationKind::Move, {}, 0, 0, 0, {}});
        operations.PostEvent({ffui::FileOperationEventKind::Cancelled, ffui::FileOperationKind::Move, {}, 0, 1, 0, {}});
        unsigned int copyProgress = 0;
        bool quantifiedProgress = false;
        bool nextOperationProgress = false;
        bool failurePreserved = false;
        std::vector<ffui::FileOperationEventKind> controls;
        MSG posted{};
        while (PeekMessageW(&posted, window, ffui::WM_APP_FILE_OPERATION_EVENT,
                            ffui::WM_APP_FILE_OPERATION_EVENT, PM_REMOVE)) {
            std::unique_ptr<ffui::FileOperationEvent> observed(
                reinterpret_cast<ffui::FileOperationEvent*>(posted.lParam));
            if (observed->kind == ffui::FileOperationEventKind::Progress) {
                if (observed->operation == ffui::FileOperationKind::Copy) {
                    ++copyProgress;
                    quantifiedProgress = quantifiedProgress || observed->total > 0;
                } else {
                    nextOperationProgress = true;
                }
            } else {
                controls.push_back(observed->kind);
                if (observed->kind == ffui::FileOperationEventKind::Completed) {
                    failurePreserved = observed->completed == 99 && observed->total == 100 &&
                        observed->failures.size() == 1 && observed->failures.front().path == L"burst-failure" &&
                        observed->failures.front().error == E_ACCESSDENIED;
                }
            }
        }
        const std::vector expectedControls{
            ffui::FileOperationEventKind::Queued, ffui::FileOperationEventKind::Started,
            ffui::FileOperationEventKind::Queued, ffui::FileOperationEventKind::Completed,
            ffui::FileOperationEventKind::Started, ffui::FileOperationEventKind::Cancelled};
        Check(copyProgress <= 3u + static_cast<unsigned int>(burstElapsed.count() / 34),
              "a progress burst flooded the posted-message queue");
        Check(quantifiedProgress && nextOperationProgress,
              "progress cadence lost first quantified progress or the next operation's first progress");
        Check(controls == expectedControls && failurePreserved,
              "progress pressure lost/reordered lifecycle or terminal failure details");
    }

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
    CheckRestoreConflictBatches(operations, root);

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

    // Conflict prompt lifetime across Stop (PA-010): Stop() can run from
    // inside the conflict dialog's nested message pump (WM_DESTROY). The
    // prompt object must outlive the worker's join so the UI thread's later
    // answer cannot write to freed memory, and the worker must resolve the
    // outstanding prompt as Cancel instead of hanging.
    {
        WriteText(source / L"stop-conflict.txt", "incoming");
        WriteText(destination / L"stop-conflict.txt", "existing");
        holdConflictQuestion = true;
        heldQuestion.reset();
        conflictQuestionCount = 0;
        lastEvent.reset();
        const int terminalsBefore = terminalEventCount;
        ffui::FileOperationRequest stopConflict{};
        stopConflict.kind = ffui::FileOperationKind::Copy;
        stopConflict.sources = {(source / L"stop-conflict.txt").wstring()};
        stopConflict.destination = destination.wstring();
        operations.Enqueue(std::move(stopConflict));
        MSG message{};
        {
            const ULONGLONG deadline = GetTickCount64() + 30000;
            while (!heldQuestion && GetTickCount64() < deadline) {
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
                MsgWaitForMultipleObjects(0, nullptr, FALSE, 25, QS_ALLINPUT);
            }
        }
        Check(heldQuestion != nullptr, "conflict prompt was not posted");
        operations.Stop();
        // Answering after Stop() is exactly what the nested dialog pump does
        // when the owner window is destroyed mid-prompt.
        auto answeredAfterStop = heldQuestion;
        heldQuestion.reset();
        if (answeredAfterStop) {
            {
                std::lock_guard lock(answeredAfterStop->mutex);
                answeredAfterStop->decision = {ffui::ConflictChoice::Cancel, false};
                answeredAfterStop->answered = true;
            }
            answeredAfterStop->answeredCondition.notify_one();
        }
        holdConflictQuestion = false;
        {
            const ULONGLONG deadline = GetTickCount64() + 30000;
            while (terminalEventCount == terminalsBefore && GetTickCount64() < deadline) {
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
                MsgWaitForMultipleObjects(0, nullptr, FALSE, 25, QS_ALLINPUT);
            }
        }
        Check(lastEvent && lastEvent->kind == ffui::FileOperationEventKind::Cancelled &&
              ReadText(destination / L"stop-conflict.txt") == "existing",
              "Stop during an in-flight conflict prompt did not resolve as Cancel");
        Check(operations.Start(window), "worker did not restart after conflict-prompt Stop");
    }

    // Last-operation failure detail comes from terminal events (PA-013): a
    // Progress tick never wipes the retained list, a terminal event replaces
    // it, and a later clean completion clears it.
    {
        const auto sourceBusy = ffui::DescribeFileOperationFailure(COPYENGINE_E_SHARING_VIOLATION_SRC);
        const auto destinationBusy = ffui::DescribeFileOperationFailure(COPYENGINE_E_SHARING_VIOLATION_DEST);
        Check(sourceBusy.find(L"source") != std::wstring::npos &&
              sourceBusy.find(L"Close the app") != std::wstring::npos &&
              sourceBusy.find(L"0x80270027") != std::wstring::npos,
              "real Shell source sharing failure lost its identity, recovery or diagnostic code");
        Check(destinationBusy.find(L"destination") != std::wstring::npos &&
              destinationBusy.find(L"0x80270028") != std::wstring::npos,
              "destination sharing failure was confused with source sharing failure");
        const auto unknown = ffui::DescribeFileOperationFailure(static_cast<HRESULT>(0x81234567u));
        Check(!unknown.empty() && unknown.find(L"0x81234567") != std::wstring::npos,
              "unrecognized failure lost its original diagnostic code");
        std::vector<ffui::FileOperationFailure> retained;
        ffui::FileOperationEvent progress{ffui::FileOperationEventKind::Progress,
                                          ffui::FileOperationKind::Copy, {}, 0, 0, 50, {}};
        ffui::FileOperationEvent failed{ffui::FileOperationEventKind::Completed,
                                        ffui::FileOperationKind::Copy, {}, 0, 2, 100,
                                        {{L"C:\\missing.txt", HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)}}};
        ffui::RetainLastOperationFailures(retained, failed);
        Check(retained.size() == 1, "terminal event failures were not retained");
        ffui::RetainLastOperationFailures(retained, progress);
        Check(retained.size() == 1, "a progress event wiped the retained failure list");
        ffui::FileOperationEvent success{ffui::FileOperationEventKind::Completed,
                                         ffui::FileOperationKind::Copy, {}, 2, 2, 100, {}};
        ffui::RetainLastOperationFailures(retained, success);
        Check(retained.empty(), "a later successful terminal event did not clear the retained list");
    }

    // Skip accounting and cancel-vs-enqueue scope (PA-024/PA-025).
    {
        const auto skipSource = root / L"skip-source";
        const auto skipDestination = root / L"skip-destination";
        std::filesystem::create_directories(skipSource);
        std::filesystem::create_directories(skipDestination);
        std::vector<std::wstring> allSkipSources;
        for (int index = 0; index < 3; ++index) {
            const std::wstring name = L"all-skip-" + std::to_wstring(index) + L".txt";
            WriteText(skipSource / name, "incoming");
            WriteText(skipDestination / name, "existing");
            allSkipSources.push_back((skipSource / name).wstring());
        }
        conflictQuestionCount = 0;
        conflictDecision = {ffui::ConflictChoice::Skip, true};
        event = Run(operations, {ffui::FileOperationKind::Copy, allSkipSources, skipDestination.wstring(), {}, true});
        Check(event->kind == ffui::FileOperationEventKind::Completed && event->completed == 0 &&
              event->skipped == 3 && event->total == 3 && event->failures.empty(),
              "all-skip batch did not report three skipped items");

        std::vector<std::wstring> mixedSources;
        for (int index = 0; index < 5; ++index) {
            const std::wstring name = L"mixed-" + std::to_wstring(index) + L".txt";
            WriteText(skipSource / name, "incoming");
            if (index < 2) WriteText(skipDestination / name, "existing");
            mixedSources.push_back((skipSource / name).wstring());
        }
        conflictQuestionCount = 0;
        conflictDecision = {ffui::ConflictChoice::Skip, false};
        event = Run(operations, {ffui::FileOperationKind::Copy, mixedSources, skipDestination.wstring(), {}, true});
        Check(event->kind == ffui::FileOperationEventKind::Completed && event->completed == 3 &&
              event->skipped == 2 && event->total == 5 && event->failures.empty(),
              "mixed skip batch did not report completed and skipped counts");

        // An Escape with nothing outstanding must not latch into the next
        // enqueued batch (D4): the cancel scope is what was running or queued
        // at cancel time, not work enqueued afterwards.
        Check(!operations.HasOutstandingWork(), "idle cancel fixture must have drained");
        operations.CancelCurrent();
        WriteText(source / L"after-idle-escape.txt", "hello");
        event = Run(operations, {ffui::FileOperationKind::Copy,
            {(source / L"after-idle-escape.txt").wstring()}, destination.wstring(), {}, true});
        Check(event->kind == ffui::FileOperationEventKind::Completed && event->failures.empty() &&
              std::filesystem::exists(destination / L"after-idle-escape.txt"),
              "an idle Escape pre-cancelled the next batch");
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
