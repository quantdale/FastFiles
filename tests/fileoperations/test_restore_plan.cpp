#include "FileOperationPolicy.h"
#include "../TestSupport.h"

#include <set>

using namespace ffui;
using namespace fftest;

int main() {
    const std::vector<ReversiblePath> paths{
        {L"C:\\restored\\one.txt", {}, {}}, {L"D:\\other\\two.txt", {}, {}},
        {L"C:\\restored\\three.txt", {}, {}}, {L"C:\\restored\\fresh.txt", {}, {}}};
    const std::set<std::wstring> occupied{paths[0].originalPath, paths[1].originalPath,
        paths[2].originalPath, L"C:\\restored\\one (2).txt"};
    const auto exists = [&](const std::wstring& path) { return occupied.contains(path); };
    for (const auto choice : {ConflictChoice::Replace, ConflictChoice::Skip, ConflictChoice::KeepBoth}) {
        int prompts = 0;
        const auto plan = BuildRestorePlan(paths, exists, [&](const std::wstring&, const std::wstring&) {
            ++prompts;
            return ConflictDecision{choice, true};
        });
        Check(plan && prompts == 1, "restore apply-to-all spans all collisions in the request");
        if (!plan) continue;
        Check(plan->size() == (choice == ConflictChoice::Skip ? 1u : 4u), "skip omits only collisions, not the noncollision");
        if (plan->empty()) continue;
        Check(plan->back().restoreIndex == 3, "noncolliding source identity retained");
        if (choice == ConflictChoice::KeepBoth) {
            if (plan->size() < 2) { Check(false, "keep-both retains both first restore identities"); continue; }
            Check(plan->front().destinationName == L"one (3).txt" && (*plan)[1].destinationName == L"two (2).txt",
                  "restore keep-both respects real suffixes and each original folder");
        }
        int nextPrompts = 0;
        const auto next = BuildRestorePlan(paths, exists, [&](const std::wstring&, const std::wstring&) {
            ++nextPrompts;
            return ConflictDecision{ConflictChoice::Cancel, false};
        });
        Check(!next && nextPrompts == 1, "restore apply-to-all never leaks into the next request");
    }
    int prompts = 0;
    const auto cancelled = BuildRestorePlan(paths, exists, [&](const std::wstring&, const std::wstring&) {
        return ConflictDecision{++prompts == 1 ? ConflictChoice::Skip : ConflictChoice::Cancel, false};
    });
    Check(!cancelled && prompts == 2, "unchecked choice prompts again and later Cancel discards the whole plan");
    const std::vector<ReversiblePath> duplicate{{L"C:\\restored\\same.txt", {}, {}}, {L"C:\\restored\\same.txt", {}, {}}};
    const auto reserved = BuildRestorePlan(duplicate, [](const std::wstring&) { return false; },
        [](const std::wstring&, const std::wstring&) { return ConflictDecision{ConflictChoice::Replace, true}; });
    Check(reserved && reserved->size() == 2 && (*reserved)[1].destinationName == L"same (2).txt" && !(*reserved)[1].replacesExisting,
          "replace never overwrites an earlier restore in the same plan");
    return FailureCount();
}
