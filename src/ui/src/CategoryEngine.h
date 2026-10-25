#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ffui {

struct CategoryDefinition {
    std::wstring id;
    std::wstring displayName;
    std::vector<std::wstring> extensions; // lowercase, with leading dots
};

struct CategoryMatch {
    std::wstring categoryId;
    std::wstring categoryName;
    bool matched = false;
};

class CategoryEngine {
public:
    CategoryEngine();

    void LoadDefaults();
    bool LoadFromSettings(const std::vector<std::pair<std::wstring, std::wstring>>& categories);
    std::vector<std::pair<std::wstring, std::wstring>> SaveToSettings() const;

    CategoryMatch Match(const std::wstring& fileName) const;
    // The canonical predicate shared by list and Treemap filtering. An empty
    // category ID means all categories; the reserved fallback ID "other"
    // matches files that have no configured category.
    bool Matches(const std::wstring& fileName, const std::wstring& categoryId) const;
    std::wstring UncategorizedFilterId() const;
    std::vector<CategoryDefinition> GetCategories() const { return categories_; }
    // Replaces the category set and rebuilds the extension lookup so Match()
    // never consults a stale index.
    void SetCategories(const std::vector<CategoryDefinition>& categories);

private:
    void RebuildExtensionIndex();

    std::vector<CategoryDefinition> categories_;
    std::map<std::wstring, std::wstring> extensionToCategory_; // lowercase ext -> category id
};

} // namespace ffui
