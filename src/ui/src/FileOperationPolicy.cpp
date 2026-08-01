#include "FileOperationPolicy.h"

#include <cwctype>
#include <filesystem>
#include <unordered_set>

namespace ffui {

namespace {

std::wstring Upper(std::wstring value) {
    for (auto& character : value) character = static_cast<wchar_t>(std::towupper(character));
    return value;
}

std::wstring Join(const std::wstring& folder, const std::wstring& name) {
    return (std::filesystem::path(folder) / name).wstring();
}

} // namespace

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
    std::optional<ConflictChoice> remainingChoice;
    for (const auto& source : sources) {
        const std::wstring originalName = std::filesystem::path(source).filename().wstring();
        const std::wstring destinationPath = Join(destinationFolder, originalName);
        if (!exists(destinationPath)) {
            result.push_back({source, {}, false});
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
        if (choice == ConflictChoice::Replace) {
            result.push_back({source, {}, true});
            continue;
        }
        const std::wstring newName = GenerateKeepBothName(destinationFolder, originalName, exists);
        if (newName.empty()) return std::nullopt;
        result.push_back({source, newName, false});
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
