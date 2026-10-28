#pragma once

#include <functional>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace ffui {

// preserve-execution-conflict-consent: known user cancellation is not an IO
// failure. Other errors remain failures even when cancellation was requested.
bool IsRequestedCancellationFailure(std::int32_t error, bool requested) noexcept;

// protect-generated-transfer-results: lexical Windows namespace overlap,
// including either ancestor direction. This does not resolve filesystem aliases.
bool TransferPathsOverlap(const std::wstring& left, const std::wstring& right);

enum class ConflictChoice { Replace, Skip, KeepBoth, Cancel };

struct ConflictDecision {
    ConflictChoice choice = ConflictChoice::Cancel;
    bool applyToAllRemaining = false;
};

struct TransferPlanItem {
    std::wstring source;
    std::wstring destinationName;
    bool replacesExisting = false;
};

using PathExists = std::function<bool(const std::wstring&)>;
using ConflictPrompt = std::function<ConflictDecision(const std::wstring&, const std::wstring&)>;

bool IsValidFileName(const std::wstring& name, std::wstring* reason = nullptr);
std::wstring GenerateKeepBothName(const std::wstring& destinationFolder, const std::wstring& originalName,
                                  const PathExists& exists);
std::optional<std::vector<TransferPlanItem>> BuildTransferPlan(
    const std::vector<std::wstring>& sources, const std::wstring& destinationFolder,
    const PathExists& exists, const ConflictPrompt& prompt);
// complete-ole-drag-source-move-effect D3: plans `.lnk` destination names for a
// batch of link sources. Reservations are tracked within the batch exactly
// like BuildTransferPlan, so two sources that share a basename receive
// distinct names instead of the later shortcut overwriting the earlier one.
std::vector<TransferPlanItem> BuildLinkPlan(const std::vector<std::wstring>& sources,
                                            const std::wstring& destinationFolder,
                                            const PathExists& exists);

enum class ReversibleOperationKind { Rename, Move, RecycleDelete };

struct ReversiblePath {
    std::wstring originalPath;
    std::wstring resultingPath;
    std::vector<std::byte> shellItemId;
};

struct RestorePlanItem {
    size_t restoreIndex = 0;
    std::wstring destinationName;
    bool replacesExisting = false;
};

// Plans the entire restore before any Shell mutation is submitted. Indices
// retain the original opaque Shell identity; destination reservations span
// folders and conflict choices are scoped to this request only.
std::optional<std::vector<RestorePlanItem>> BuildRestorePlan(
    const std::vector<ReversiblePath>& paths, const PathExists& exists,
    const ConflictPrompt& prompt);

struct ReversibleOperation {
    ReversibleOperationKind kind;
    std::vector<ReversiblePath> paths;
};

class OperationHistory {
public:
    void PushRename(std::wstring originalPath, std::wstring resultingPath);
    void PushMove(std::vector<ReversiblePath> paths);
    void PushRecycleDelete(std::vector<ReversiblePath> paths);
    bool Empty() const noexcept;
    size_t Size() const noexcept;
    std::optional<ReversibleOperation> Pop();

private:
    std::vector<ReversibleOperation> entries_;
};

} // namespace ffui
