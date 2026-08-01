#pragma once

#include <functional>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace ffui {

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

enum class ReversibleOperationKind { Rename, Move, RecycleDelete };

struct ReversiblePath {
    std::wstring originalPath;
    std::wstring resultingPath;
    std::vector<std::byte> shellItemId;
};

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
