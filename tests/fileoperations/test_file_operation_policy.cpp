#include "FileOperationPolicy.h"
#include "ConflictDialog.h"
#include "OleDragDrop.h"

#include <cstdlib>
#include <iostream>
#include <shlobj.h>
#include <sherrors.h>
#include <unordered_set>
#include <thread>
#include <windows.h>
#include <shellapi.h>
#include "../TestSupport.h"

using namespace fftest;


int main() {
    using namespace ffui;
    Check(IsRequestedCancellationFailure(static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_CANCELLED)), true),
          "requested Win32 cancellation is not an IO failure");
    Check(IsRequestedCancellationFailure(static_cast<std::int32_t>(COPYENGINE_E_USER_CANCELLED), true),
          "requested Shell user cancellation is not an IO failure");
    for (const auto error : {HRESULT_FROM_WIN32(ERROR_CANCELLED), COPYENGINE_E_USER_CANCELLED}) {
        Check(!IsRequestedCancellationFailure(static_cast<std::int32_t>(error), false),
              "unrequested cancellation result is retained for diagnosis");
    }
    for (const auto error : {E_ACCESSDENIED, HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION),
                             HRESULT_FROM_WIN32(ERROR_OPERATION_ABORTED), COPYENGINE_E_CANCELLED, E_ABORT, E_FAIL}) {
        Check(!IsRequestedCancellationFailure(static_cast<std::int32_t>(error), true),
              "genuine or ambiguous error survives concurrent cancellation");
    }

    Check(SUCCEEDED(OleInitialize(nullptr)), "OLE initialization failed");
    std::wstring reason;
    Check(IsValidFileName(L"quarterly report.txt", &reason), "valid name rejected");
    Check(!IsValidFileName(L"bad?.txt", &reason) && !reason.empty(), "reserved character accepted");
    Check(!IsValidFileName(L"CON.txt", &reason), "device name accepted");
    Check(!IsValidFileName(L"trailing. ", &reason), "trailing space accepted");
    Check(IsValidFileName(std::wstring(255, L'a'), &reason), "maximum Windows component length rejected");
    Check(!IsValidFileName(std::wstring(256, L'a'), &reason), "overlong Windows component accepted");

    std::unordered_set<std::wstring> occupied{L"D:\\target\\file.txt", L"D:\\target\\file (2).txt"};
    const auto exists = [&](const std::wstring& path) { return occupied.contains(path); };
    Check(GenerateKeepBothName(L"D:\\target", L"file.txt", exists) == L"file (3).txt", "keep-both suffix wrong");

    int prompts = 0;
    const auto plan = BuildTransferPlan({L"C:\\source\\file.txt", L"C:\\source\\other.txt"}, L"D:\\target", exists,
        [&](const std::wstring&, const std::wstring&) {
            ++prompts;
            return ConflictDecision{ConflictChoice::KeepBoth, true};
        });
    Check(plan && plan->size() == 2 && (*plan)[0].destinationName == L"file (3).txt", "keep-both plan wrong");
    Check(prompts == 1, "apply-to-all prompted more than once");

    int externalReplacePrompts = 0;
    const auto externalReplace = BuildTransferPlan({L"C:\\source\\file.txt"}, L"D:\\target", exists,
        [&](const std::wstring&, const std::wstring&) {
            ++externalReplacePrompts;
            return ConflictDecision{ConflictChoice::Replace, false};
        });
    Check(externalReplace && externalReplace->size() == 1 &&
              (*externalReplace)[0].destinationName.empty() &&
              (*externalReplace)[0].replacesExisting && externalReplacePrompts == 1,
          "Replace still applies to an existing destination item");

    occupied.insert(L"D:\\target\\other.txt");
    prompts = 0;
    const auto skipped = BuildTransferPlan({L"C:\\source\\file.txt", L"C:\\source\\other.txt"}, L"D:\\target", exists,
        [&](const std::wstring&, const std::wstring&) {
            ++prompts;
            return ConflictDecision{ConflictChoice::Skip, true};
        });
    Check(skipped && skipped->empty() && prompts == 1, "skip/apply-all plan wrong");

    int duplicatePrompts = 0;
    const auto duplicateNames = BuildTransferPlan(
        {L"C:\\source-a\\duplicate.txt", L"C:\\source-b\\duplicate.txt"}, L"D:\\target",
        [](const std::wstring&) { return false; },
        [&](const std::wstring&, const std::wstring&) {
            ++duplicatePrompts;
            return ConflictDecision{ConflictChoice::KeepBoth, false};
        });
    Check(duplicateNames && duplicateNames->size() == 2 &&
              (*duplicateNames)[0].destinationName.empty() &&
              (*duplicateNames)[1].destinationName == L"duplicate (2).txt" &&
              duplicatePrompts == 1,
          "duplicate source names were not assigned distinct destinations");

    int duplicateReplacePrompts = 0;
    const auto duplicateReplace = BuildTransferPlan(
        {L"C:\\source-a\\duplicate.txt", L"C:\\source-b\\duplicate.txt"}, L"D:\\target",
        [](const std::wstring&) { return false; },
        [&](const std::wstring&, const std::wstring&) {
            ++duplicateReplacePrompts;
            return ConflictDecision{ConflictChoice::Replace, true};
        });
    Check(duplicateReplace && duplicateReplace->size() == 2 &&
              (*duplicateReplace)[0].destinationName.empty() &&
              !(*duplicateReplace)[0].replacesExisting &&
              (*duplicateReplace)[1].destinationName == L"duplicate (2).txt" &&
              !(*duplicateReplace)[1].replacesExisting && duplicateReplacePrompts == 1,
          "Replace cannot overwrite an earlier item in the same transfer batch");

    int unicodeCasePrompts = 0;
    const auto unicodeCaseNames = BuildTransferPlan(
        {L"C:\\source-a\\é.txt", L"C:\\source-b\\É.txt"}, L"D:\\target",
        [](const std::wstring&) { return false; },
        [&](const std::wstring&, const std::wstring&) {
            ++unicodeCasePrompts;
            return ConflictDecision{ConflictChoice::KeepBoth, false};
        });
    Check(unicodeCaseNames && unicodeCaseNames->size() == 2 &&
              (*unicodeCaseNames)[0].destinationName.empty() &&
              (*unicodeCaseNames)[1].destinationName == L"É (2).txt" &&
              unicodeCasePrompts == 1,
          "Unicode case-variant source names were assigned distinct destinations");

    // complete-ole-drag-source-move-effect 3.1/3.2: only a MOVE whose sources
    // still exist is completed by the source (Recycle-Bin delete); a target
    // that moved the files itself, and every COPY/LINK, leaves sources alone.
    {
        const std::vector<std::wstring> dragSources{L"C:\\source\\one.txt", L"C:\\source\\two.txt"};
        const auto allExist = [](const std::wstring&) { return true; };
        const auto noneExist = [](const std::wstring&) { return false; };
        Check(SourcePathsToDeleteAfterDrag(DROPEFFECT_MOVE, dragSources, allExist) == dragSources,
              "a target that copied then reported MOVE leaves sources to delete");
        Check(SourcePathsToDeleteAfterDrag(DROPEFFECT_MOVE, dragSources, noneExist).empty(),
              "a target that already moved the files is not double-deleted");
        Check(SourcePathsToDeleteAfterDrag(DROPEFFECT_COPY, dragSources, allExist).empty(),
              "copy drag-out never deletes sources");
        Check(SourcePathsToDeleteAfterDrag(DROPEFFECT_LINK, dragSources, allExist).empty(),
              "link drag-out never deletes sources");
        const auto partialDelete = SourcePathsToDeleteAfterDrag(DROPEFFECT_MOVE, dragSources,
            [](const std::wstring& path) { return path == L"C:\\source\\one.txt"; });
        Check(partialDelete.size() == 1 && partialDelete.front() == L"C:\\source\\one.txt",
              "only sources still present after the drag are deleted");
    }

    // complete-ole-drag-source-move-effect 3.3: intra-batch .lnk reservation.
    {
        const auto noFiles = [](const std::wstring&) { return false; };
        const auto links = BuildLinkPlan({L"D:\\a\\report.txt", L"E:\\b\\report.txt"}, L"C:\\links", noFiles);
        Check(links.size() == 2 && links[0].destinationName == L"report.txt.lnk" &&
              links[1].destinationName == L"report.txt (2).lnk",
              "two same-basename link sources received distinct .lnk names");
        const auto occupiedLink = [](const std::wstring& path) { return path == L"C:\\links\\report.txt.lnk"; };
        const auto reserved = BuildLinkPlan({L"D:\\a\\report.txt", L"E:\\b\\report.txt"}, L"C:\\links", occupiedLink);
        Check(reserved.size() == 2 && reserved[0].destinationName == L"report.txt (2).lnk" &&
              reserved[1].destinationName == L"report.txt (3).lnk",
              "link planning reserves names within the batch and against disk");
    }

    OperationHistory history;
    history.PushRename(L"C:\\old.txt", L"C:\\new.txt");
    history.PushMove({{L"C:\\one.txt", L"D:\\one.txt"}});
    Check(history.Size() == 2, "history size wrong");
    const auto move = history.Pop();
    Check(move && move->kind == ReversibleOperationKind::Move, "history is not LIFO");
    const auto rename = history.Pop();
    Check(rename && rename->kind == ReversibleOperationKind::Rename && history.Empty(), "rename history wrong");

    std::thread dialogDriver([] {
        HWND dialog = nullptr;
        for (int attempt = 0; attempt < 100 && dialog == nullptr; ++attempt) {
            const HWND candidate = FindWindowW(L"FastFilesConflictDialog", nullptr);
            if (candidate != nullptr && IsWindowVisible(candidate) &&
                GetDlgItem(candidate, 1004) != nullptr && GetDlgItem(candidate, 1003) != nullptr) {
                dialog = candidate;
            } else {
                Sleep(10);
            }
        }
        Check(dialog != nullptr, "conflict dialog did not appear");
        SendMessageW(GetDlgItem(dialog, 1004), BM_SETCHECK, BST_CHECKED, 0);
        PostMessageW(dialog, WM_COMMAND, 1003, 0);
    });
    const auto dialogDecision = ShowConflictDialog(GetDesktopWindow(), L"C:\\source\\file.txt", L"D:\\target\\file.txt");
    dialogDriver.join();
    Check(dialogDecision.choice == ConflictChoice::KeepBoth && dialogDecision.applyToAllRemaining,
          "custom conflict dialog result wrong");

    IDataObject* dataObject = nullptr;
    Check(SUCCEEDED(CreateFileDataObject({L"C:\\source\\one.txt", L"C:\\source\\two.txt"}, &dataObject)),
          "CF_HDROP data object creation failed");
    FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium{};
    Check(SUCCEEDED(dataObject->GetData(&format, &medium)), "CF_HDROP extraction failed");
    const HDROP drop = static_cast<HDROP>(GlobalLock(medium.hGlobal));
    Check(drop != nullptr && DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0) == 2, "CF_HDROP path count wrong");
    GlobalUnlock(medium.hGlobal);
    ReleaseStgMedium(&medium);

    // complete-ole-drag-source-move-effect D1/D2: preferred-effect advertising
    // and the performed-effect round trip on the source's own data object.
    IDataObject* effectObject = nullptr;
    Check(SUCCEEDED(CreateFileDataObject({L"C:\\source\\one.txt"}, DROPEFFECT_COPY, &effectObject)),
          "data object with a preferred effect was not created");
    if (effectObject != nullptr) {
        const UINT preferredFormat = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
        FORMATETC preferred{static_cast<CLIPFORMAT>(preferredFormat), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM preferredMedium{};
        DWORD preferredValue = 0;
        const bool advertised = SUCCEEDED(effectObject->GetData(&preferred, &preferredMedium)) &&
                                preferredMedium.hGlobal != nullptr;
        if (advertised) {
            void* data = GlobalLock(preferredMedium.hGlobal);
            if (data != nullptr) {
                preferredValue = *static_cast<const DWORD*>(data);
                GlobalUnlock(preferredMedium.hGlobal);
            }
            ReleaseStgMedium(&preferredMedium);
        }
        Check(advertised && preferredValue == DROPEFFECT_COPY,
              "the preferred drop effect is advertised on the data object");

        HGLOBAL performedStorage = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
        Check(performedStorage != nullptr, "performed-effect fixture allocation failed");
        if (performedStorage != nullptr) {
            auto* value = static_cast<DWORD*>(GlobalLock(performedStorage));
            Check(value != nullptr, "performed-effect fixture lock failed");
            if (value != nullptr) {
                *value = DROPEFFECT_MOVE;
                GlobalUnlock(performedStorage);
            }
            const UINT performedFormat = RegisterClipboardFormatW(CFSTR_PERFORMEDDROPEFFECT);
            FORMATETC performed{static_cast<CLIPFORMAT>(performedFormat), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
            STGMEDIUM performedMedium{};
            performedMedium.tymed = TYMED_HGLOBAL;
            performedMedium.hGlobal = performedStorage;
            performedMedium.pUnkForRelease = nullptr;
            Check(SUCCEEDED(effectObject->SetData(&performed, &performedMedium, TRUE)),
                  "the data object did not accept the target's performed drop effect");
            Check(PerformedDropEffectFromDataObject(effectObject) == DROPEFFECT_MOVE,
                  "the performed drop effect was not read back from the data object");
        }
        effectObject->Release();
    }
    Check(DetermineDropEffect(0, DROPEFFECT_COPY | DROPEFFECT_MOVE, {L"C:\\one.txt"}, L"C:\\target") == DROPEFFECT_MOVE,
          "same-volume default drag effect wrong");
    Check(DetermineDropEffect(0, DROPEFFECT_COPY | DROPEFFECT_MOVE, {L"C:\\one.txt"}, L"D:\\target") == DROPEFFECT_COPY,
          "cross-volume default drag effect wrong");
    Check(DetermineDropEffect(MK_CONTROL | MK_SHIFT, DROPEFFECT_LINK, {L"C:\\one.txt"}, L"C:\\target") == DROPEFFECT_LINK,
          "Ctrl+Shift link drag effect wrong");

    bool dropCalled = false;
    IDropTarget* dropTarget = nullptr;
    Check(SUCCEEDED(CreateFileDropTarget([](POINTL) { return std::wstring(L"C:\\target"); },
        [&](std::vector<std::wstring> paths, DWORD effect, std::wstring destination) {
            // stabilize-live-uia-e2e: the executed destination must be the
            // drop-point destination, not a re-derived active-pane path.
            dropCalled = paths.size() == 2 && effect == DROPEFFECT_MOVE && destination == L"C:\\target";
        }, &dropTarget)), "drop target creation failed");
    DWORD effect = DROPEFFECT_COPY | DROPEFFECT_MOVE;
    POINTL point{};
    Check(SUCCEEDED(dropTarget->DragEnter(dataObject, 0, point, &effect)) && effect == DROPEFFECT_MOVE,
          "drop target DragEnter negotiation failed");
    effect = DROPEFFECT_COPY | DROPEFFECT_MOVE;
    Check(SUCCEEDED(dropTarget->Drop(dataObject, 0, point, &effect)) && dropCalled,
          "drop target did not extract and route CF_HDROP");
    dropTarget->Release();
    dataObject->Release();

    std::cout << "file operation policy tests passed\n";
    OleUninitialize();
    return fftest::FailureCount();
}
