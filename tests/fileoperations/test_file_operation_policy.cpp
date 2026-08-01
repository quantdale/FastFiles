#include "FileOperationPolicy.h"
#include "ConflictDialog.h"
#include "OleDragDrop.h"

#include <cstdlib>
#include <iostream>
#include <unordered_set>
#include <thread>
#include <windows.h>
#include <shellapi.h>

namespace {

void Check(bool value, const char* message) {
    if (!value) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main() {
    using namespace ffui;
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

    occupied.insert(L"D:\\target\\other.txt");
    prompts = 0;
    const auto skipped = BuildTransferPlan({L"C:\\source\\file.txt", L"C:\\source\\other.txt"}, L"D:\\target", exists,
        [&](const std::wstring&, const std::wstring&) {
            ++prompts;
            return ConflictDecision{ConflictChoice::Skip, true};
        });
    Check(skipped && skipped->empty() && prompts == 1, "skip/apply-all plan wrong");

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
            dialog = FindWindowW(L"FastFilesConflictDialog", nullptr);
            if (dialog == nullptr) Sleep(10);
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
    Check(DetermineDropEffect(0, DROPEFFECT_COPY | DROPEFFECT_MOVE, {L"C:\\one.txt"}, L"C:\\target") == DROPEFFECT_MOVE,
          "same-volume default drag effect wrong");
    Check(DetermineDropEffect(0, DROPEFFECT_COPY | DROPEFFECT_MOVE, {L"C:\\one.txt"}, L"D:\\target") == DROPEFFECT_COPY,
          "cross-volume default drag effect wrong");
    Check(DetermineDropEffect(MK_CONTROL | MK_SHIFT, DROPEFFECT_LINK, {L"C:\\one.txt"}, L"C:\\target") == DROPEFFECT_LINK,
          "Ctrl+Shift link drag effect wrong");

    bool dropCalled = false;
    IDropTarget* dropTarget = nullptr;
    Check(SUCCEEDED(CreateFileDropTarget([] { return std::wstring(L"C:\\target"); },
        [&](std::vector<std::wstring> paths, DWORD effect) {
            dropCalled = paths.size() == 2 && effect == DROPEFFECT_MOVE;
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
}
