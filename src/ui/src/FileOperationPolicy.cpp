#include "FileOperationPolicy.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <set>
#include <unordered_set>

#include "ffprotocol/WindowsPath.h"
#include <sherrors.h>

namespace ffui {

bool IsRequestedCancellationFailure(std::int32_t error, bool requested) noexcept {
    return requested && (error == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_CANCELLED)) ||
                         error == static_cast<std::int32_t>(COPYENGINE_E_USER_CANCELLED));
}

namespace {

std::wstring Upper(std::wstring value) {
    for (auto& character : value) character = static_cast<wchar_t>(std::towupper(character));
    return value;
}

std::wstring Join(const std::wstring& folder, const std::wstring& name) {
    return (std::filesystem::path(folder) / name).wstring();
}

std::wstring DestinationKey(const std::wstring& path) {
    return std::filesystem::path(path).lexically_normal().wstring();
}

std::wstring NormalizeTransferPath(const std::wstring& path) {
    std::wstring value = DestinationKey(path);
    if (value.size() >= 8 && ffprotocol::WindowsPathEqual(value.substr(0, 8), L"\\\\?\\UNC\\")) value = L"\\\\" + value.substr(8);
    else if (value.starts_with(L"\\\\?\\")) value.erase(0, 4);
    std::replace(value.begin(), value.end(), L'/', L'\\');
    while (!value.empty() && value.back() == L'\\') value.pop_back();
    return value;
}

bool PathContains(const std::wstring& parent, const std::wstring& child) {
    if (child.size() < parent.size()) return false;
    if (!ffprotocol::WindowsPathEqual(parent, std::wstring_view(child).substr(0, parent.size()))) return false;
    return child.size() == parent.size() || child[parent.size()] == L'\\';
}

} // namespace

bool TransferPathsOverlap(const std::wstring& left, const std::wstring& right) {
    const auto a = NormalizeTransferPath(left);
    const auto b = NormalizeTransferPath(right);
    // An unavailable actual result name cannot establish that replacement is safe.
    if (a.empty() || b.empty()) return true;
    return PathContains(a, b) || PathContains(b, a);
}

ReplacementSafety EvaluateReplacementSafety(size_t submittedOrdinary, bool ordinaryFailed,
    const std::vector<ReversiblePath>& ordinaryResults, const std::vector<ReplacementTarget>& replacements) {
    if (ordinaryFailed || ordinaryResults.size() != submittedOrdinary ||
        std::any_of(ordinaryResults.begin(), ordinaryResults.end(), [](const auto& item) { return item.resultingPath.empty(); }) ||
        std::any_of(replacements.begin(), replacements.end(), [](const auto& item) { return item.destination.empty(); })) {
        return ReplacementSafety::Incomplete;
    }
    std::vector<std::wstring> ordinaryPaths;
    ordinaryPaths.reserve(ordinaryResults.size());
    for (const auto& result : ordinaryResults) {
        auto path = NormalizeTransferPath(result.resultingPath);
        if (path.empty()) return ReplacementSafety::Incomplete;
        ordinaryPaths.push_back(std::move(path));
    }
    std::vector<std::wstring> replacementPaths;
    replacementPaths.reserve(replacements.size());
    for (const auto& replacement : replacements) {
        auto path = NormalizeTransferPath(replacement.destination);
        if (path.empty()) return ReplacementSafety::Incomplete;
        replacementPaths.push_back(std::move(path));
    }
    const ffprotocol::CaseInsensitivePathLess less;
    std::sort(ordinaryPaths.begin(), ordinaryPaths.end(), less);
    for (const auto& target : replacementPaths) {
        if (std::binary_search(ordinaryPaths.begin(), ordinaryPaths.end(), target, less)) return ReplacementSafety::Overlap;
        const auto descendantPrefix = target + L"\\";
        const auto child = std::lower_bound(ordinaryPaths.begin(), ordinaryPaths.end(), descendantPrefix, less);
        if (child != ordinaryPaths.end() && PathContains(target, *child)) return ReplacementSafety::Overlap;
        for (auto separator = target.find_last_of(L'\\'); separator != std::wstring::npos && separator > 0;
             separator = target.find_last_of(L'\\', separator - 1)) {
            if (std::binary_search(ordinaryPaths.begin(), ordinaryPaths.end(), target.substr(0, separator), less)) {
                return ReplacementSafety::Overlap;
            }
        }
    }
    return ReplacementSafety::Safe;
}

bool IsValidFileName(const std::wstring& name, std::wstring* reason) {
    auto reject = [&](const wchar_t* message) {
        if (reason != nullptr) *reason = message;
        return false;
    };
    if (name.empty()) return reject(L"A name is required.");
    if (name.size() > 255) return reject(L"A Windows filename component cannot exceed 255 characters.");
    if (name == L"." || name == L"..") return reject(L"That name is reserved by Windows.");
    if (name.back() == L'.' || name.back() == L' ') return reject(L"A name cannot end with a period or space.");
    for (const wchar_t character : name) {
        if (character < 32 || std::wstring_view(L"<>:\"/\\|?*").find(character) != std::wstring_view::npos) {
            return reject(L"The name contains a character Windows does not allow.");
        }
    }
    const std::wstring stem = Upper(name.substr(0, name.find(L'.')));
    static const std::unordered_set<std::wstring> reserved{L"CON", L"PRN", L"AUX", L"NUL",
        L"COM1", L"COM2", L"COM3", L"COM4", L"COM5", L"COM6", L"COM7", L"COM8", L"COM9",
        L"LPT1", L"LPT2", L"LPT3", L"LPT4", L"LPT5", L"LPT6", L"LPT7", L"LPT8", L"LPT9"};
    if (reserved.contains(stem)) return reject(L"That name is reserved by Windows.");
    if (reason != nullptr) reason->clear();
    return true;
}

std::wstring GenerateKeepBothName(const std::wstring& destinationFolder, const std::wstring& originalName,
                                  const PathExists& exists) {
    const std::filesystem::path original(originalName);
    std::wstring stem = original.stem().wstring();
    const std::wstring extension = original.extension().wstring();
    if (stem.empty()) stem = originalName;
    for (unsigned int suffix = 2; suffix < 1000000; ++suffix) {
        const std::wstring candidate = stem + L" (" + std::to_wstring(suffix) + L")" + extension;
        if (!exists(Join(destinationFolder, candidate))) return candidate;
    }
    return {};
}

std::optional<std::vector<TransferPlanItem>> BuildTransferPlan(
    const std::vector<std::wstring>& sources, const std::wstring& destinationFolder,
    const PathExists& exists, const ConflictPrompt& prompt) {
    std::vector<TransferPlanItem> result;
    std::set<std::wstring, ffprotocol::CaseInsensitivePathLess> plannedDestinations;
    const auto isOccupied = [&](const std::wstring& path) {
        return plannedDestinations.contains(DestinationKey(path)) || exists(path);
    };
    const auto reserve = [&](const std::wstring& path) {
        plannedDestinations.insert(DestinationKey(path));
    };
    std::optional<ConflictChoice> remainingChoice;
    for (const auto& source : sources) {
        const std::wstring originalName = std::filesystem::path(source).filename().wstring();
        const std::wstring destinationPath = Join(destinationFolder, originalName);
        const bool plannedCollision = plannedDestinations.contains(DestinationKey(destinationPath));
        const bool externalCollision = exists(destinationPath);
        if (!plannedCollision && !externalCollision) {
            result.push_back({source, {}, false});
            reserve(destinationPath);
            continue;
        }
        ConflictChoice choice;
        if (remainingChoice) {
            choice = *remainingChoice;
        } else {
            const ConflictDecision decision = prompt(source, destinationPath);
            choice = decision.choice;
            if (decision.applyToAllRemaining && choice != ConflictChoice::Cancel) remainingChoice = choice;
        }
        if (choice == ConflictChoice::Cancel) return std::nullopt;
        if (choice == ConflictChoice::Skip) continue;
        // Replace is safe only for an item that existed before this batch.
        // A planned collision would otherwise overwrite an earlier source.
        if (choice == ConflictChoice::Replace && !plannedCollision) {
            result.push_back({source, {}, true});
            reserve(destinationPath);
            continue;
        }
        const std::wstring newName = GenerateKeepBothName(destinationFolder, originalName, isOccupied);
        if (newName.empty()) return std::nullopt;
        result.push_back({source, newName, false});
        reserve(Join(destinationFolder, newName));
    }
    return result;
}

std::vector<TransferPlanItem> BuildLinkPlan(const std::vector<std::wstring>& sources,
                                            const std::wstring& destinationFolder,
                                            const PathExists& exists) {
    std::vector<TransferPlanItem> result;
    std::set<std::wstring, ffprotocol::CaseInsensitivePathLess> plannedDestinations;
    const auto isOccupied = [&](const std::wstring& path) {
        return plannedDestinations.contains(DestinationKey(path)) || exists(path);
    };
    result.reserve(sources.size());
    for (const auto& source : sources) {
        const std::wstring baseName = std::filesystem::path(source).filename().wstring() + L".lnk";
        std::wstring name = baseName;
        if (isOccupied(Join(destinationFolder, name))) {
            name = GenerateKeepBothName(destinationFolder, baseName, isOccupied);
            if (name.empty()) break;
        }
        result.push_back({source, name, false});
        plannedDestinations.insert(DestinationKey(Join(destinationFolder, name)));
    }
    return result;
}

std::optional<std::vector<RestorePlanItem>> BuildRestorePlan(
    const std::vector<ReversiblePath>& paths, const PathExists& exists,
    const ConflictPrompt& prompt) {
    std::vector<RestorePlanItem> result;
    std::set<std::wstring, ffprotocol::CaseInsensitivePathLess> plannedDestinations;
    const auto isOccupied = [&](const std::wstring& path) {
        return plannedDestinations.contains(DestinationKey(path)) || exists(path);
    };
    std::optional<ConflictChoice> remainingChoice;
    for (size_t index = 0; index < paths.size(); ++index) {
        const auto& originalPath = paths[index].originalPath;
        const auto original = std::filesystem::path(originalPath);
        const std::wstring folder = original.parent_path().wstring();
        const bool plannedCollision = plannedDestinations.contains(DestinationKey(originalPath));
        if (!plannedCollision && !exists(originalPath)) {
            result.push_back({index, {}, false});
            plannedDestinations.insert(DestinationKey(originalPath));
            continue;
        }
        ConflictChoice choice;
        if (remainingChoice) {
            choice = *remainingChoice;
        } else {
            const ConflictDecision decision = prompt(originalPath, originalPath);
            choice = decision.choice;
            if (decision.applyToAllRemaining && choice != ConflictChoice::Cancel) remainingChoice = choice;
        }
        if (choice == ConflictChoice::Cancel) return std::nullopt;
        if (choice == ConflictChoice::Skip) continue;
        if (choice == ConflictChoice::Replace && !plannedCollision) {
            result.push_back({index, {}, true});
            plannedDestinations.insert(DestinationKey(originalPath));
            continue;
        }
        const std::wstring name = GenerateKeepBothName(folder, original.filename().wstring(), isOccupied);
        if (name.empty()) return std::nullopt;
        result.push_back({index, name, false});
        plannedDestinations.insert(DestinationKey(Join(folder, name)));
    }
    return result;
}

void OperationHistory::PushRename(std::wstring originalPath, std::wstring resultingPath) {
    entries_.push_back({ReversibleOperationKind::Rename, {{std::move(originalPath), std::move(resultingPath)}}});
}

void OperationHistory::PushMove(std::vector<ReversiblePath> paths) {
    if (!paths.empty()) entries_.push_back({ReversibleOperationKind::Move, std::move(paths)});
}

void OperationHistory::PushRecycleDelete(std::vector<ReversiblePath> paths) {
    if (!paths.empty()) entries_.push_back({ReversibleOperationKind::RecycleDelete, std::move(paths)});
}

bool OperationHistory::Empty() const noexcept { return entries_.empty(); }
size_t OperationHistory::Size() const noexcept { return entries_.size(); }

std::optional<ReversibleOperation> OperationHistory::Pop() {
    if (entries_.empty()) return std::nullopt;
    ReversibleOperation entry = std::move(entries_.back());
    entries_.pop_back();
    return entry;
}

} // namespace ffui
