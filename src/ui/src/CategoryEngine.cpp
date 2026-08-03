#include "CategoryEngine.h"

#include <algorithm>
#include <cwctype>
#include <sstream>

namespace ffui {

CategoryEngine::CategoryEngine() {
    LoadDefaults();
}

void CategoryEngine::LoadDefaults() {
    categories_.clear();
    extensionToCategory_.clear();

    auto addCategory = [this](const wchar_t* id, const wchar_t* name, std::initializer_list<const wchar_t*> exts) {
        CategoryDefinition def;
        def.id = id;
        def.displayName = name;
        for (auto ext : exts) {
            std::wstring lower = ext;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
            if (lower.empty() || lower[0] != L'.') lower = L"." + lower;
            def.extensions.push_back(lower);
            extensionToCategory_[lower] = id;
        }
        categories_.push_back(std::move(def));
    };

    addCategory(L"video", L"Video", {L"mp4", L"mkv", L"avi", L"mov", L"wmv", L"flv", L"webm", L"m4v", L"mpg", L"mpeg"});
    addCategory(L"image", L"Image", {L"jpg", L"jpeg", L"png", L"bmp", L"gif", L"tiff", L"tif", L"webp", L"ico", L"svg"});
    addCategory(L"document", L"Document", {L"pdf", L"doc", L"docx", L"xls", L"xlsx", L"ppt", L"pptx", L"odt", L"ods", L"odp", L"rtf", L"txt", L"md"});
    addCategory(L"archive", L"Archive", {L"zip", L"rar", L"7z", L"tar", L"gz", L"bz2", L"xz", L"iso", L"cab"});
    addCategory(L"executable", L"Executable", {L"exe", L"dll", L"msi", L"bat", L"cmd", L"ps1", L"com", L"scr"});
    addCategory(L"development", L"Development", {L"cpp", L"c", L"h", L"hpp", L"py", L"js", L"ts", L"java", L"cs", L"go", L"rs", L"rb", L"php", L"swift", L"kt", L"sql", L"json", L"xml", L"yaml", L"yml", L"toml"});
    addCategory(L"vm-image", L"VM Image", {L"vhd", L"vhdx", L"vmdk", L"qcow2", L"vdi", L"ova", L"ovf"});
    addCategory(L"game", L"Game", {L"rom", L"iso", L"bin", L"cue", L"gba", L"nds", L"sfc", L"smc", L"n64", L"z64", L"v64"});
}

bool CategoryEngine::LoadFromSettings(const std::vector<std::pair<std::wstring, std::wstring>>& categories) {
    categories_.clear();
    extensionToCategory_.clear();

    for (const auto& [name, exts] : categories) {
        CategoryDefinition def;
        def.id = name;
        def.displayName = name;
        std::wistringstream iss(exts);
        std::wstring ext;
        while (std::getline(iss, ext, L';')) {
            if (!ext.empty()) {
                std::wstring lower = ext;
                std::transform(lower.begin(), lower.end(), lower.begin(),
                               [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
                if (lower.empty() || lower[0] != L'.') lower = L"." + lower;
                def.extensions.push_back(lower);
                extensionToCategory_[lower] = name;
            }
        }
        if (!def.extensions.empty()) {
            categories_.push_back(std::move(def));
        }
    }

    if (categories_.empty()) {
        LoadDefaults();
    }
    return true;
}

std::vector<std::pair<std::wstring, std::wstring>> CategoryEngine::SaveToSettings() const {
    std::vector<std::pair<std::wstring, std::wstring>> result;
    for (const auto& cat : categories_) {
        std::wstring exts;
        for (size_t i = 0; i < cat.extensions.size(); ++i) {
            if (i > 0) exts += L";";
            exts += cat.extensions[i];
        }
        result.emplace_back(cat.id, exts);
    }
    return result;
}

CategoryMatch CategoryEngine::Match(const std::wstring& fileName) const {
    CategoryMatch result;
    const size_t dot = fileName.find_last_of(L'.');
    if (dot == std::wstring::npos || dot + 1 >= fileName.size()) {
        return result;
    }
    std::wstring ext = fileName.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    if (ext.empty() || ext[0] != L'.') ext = L"." + ext;

    auto it = extensionToCategory_.find(ext);
    if (it != extensionToCategory_.end()) {
        result.matched = true;
        result.categoryId = it->second;
        for (const auto& cat : categories_) {
            if (cat.id == it->second) {
                result.categoryName = cat.displayName;
                break;
            }
        }
    }
    return result;
}

} // namespace ffui
