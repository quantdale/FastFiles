#include "CategoryEngine.h"
#include "TreemapFilter.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>

#include "../TestSupport.h"

using namespace fftest;

namespace {

void AddFile(ffui::TreemapNode& parent, const wchar_t* name, uint64_t sizeBytes) {
    auto file = std::make_unique<ffui::TreemapNode>();
    file->name = name;
    file->path = parent.path + L"\\" + name;
    file->sizeBytes = sizeBytes;
    file->totalSizeBytes = sizeBytes;
    file->parent = &parent;
    parent.children.push_back(std::move(file));
}

ffui::TreemapNode* AddDirectory(ffui::TreemapNode& parent, const wchar_t* name) {
    auto directory = std::make_unique<ffui::TreemapNode>();
    directory->name = name;
    directory->path = parent.path + L"\\" + name;
    directory->isDirectory = true;
    directory->parent = &parent;
    ffui::TreemapNode* result = directory.get();
    parent.children.push_back(std::move(directory));
    return result;
}

std::unique_ptr<ffui::TreemapNode> BuildFixture() {
    auto root = std::make_unique<ffui::TreemapNode>();
    root->name = L"Root";
    root->path = L"C:\\Fixture";
    root->isDirectory = true;

    AddFile(*root, L"photo.jpg", 100);
    AddFile(*root, L"movie.mp4", 400);
    AddFile(*root, L"notes.txt", 50);
    ffui::TreemapNode* child = AddDirectory(*root, L"Child");
    AddFile(*child, L"icon.png", 25);
    AddFile(*child, L"archive.zip", 200);

    root->totalSizeBytes = 775;
    child->totalSizeBytes = 225;
    return root;
}

bool ContainsName(const ffui::TreemapNode& node, const std::wstring& name) {
    for (const auto& child : node.children) {
        if (child->name == name || ContainsName(*child, name)) return true;
    }
    return false;
}

size_t CountFiles(const ffui::TreemapNode& node) {
    size_t count = 0;
    for (const auto& child : node.children) {
        count += child->isDirectory ? CountFiles(*child) : 1;
    }
    return count;
}

void Apply(ffui::TreemapNode& root, const wchar_t* categoryId) {
    static const ffui::CategoryEngine engine;
    ffui::FilterTreemapTree(root, engine, categoryId);
}

void TestAllPreservesTheFullTree() {
    auto root = BuildFixture();
    Apply(*root, L"");

    Check(root->children.size() == 4, "All keeps every root entry");
    Check(CountFiles(*root) == 5, "All keeps every fixture file");
    Check(root->totalSizeBytes == 775, "All preserves the complete aggregate");
    Check(ContainsName(*root, L"archive.zip"), "All keeps the nested archive");
}

void TestImageKeepsNestedContainersAndReaggregates() {
    auto root = BuildFixture();
    Apply(*root, L"image");

    Check(root->children.size() == 2, "Image keeps the root image and required child container");
    Check(CountFiles(*root) == 2, "Image keeps only image files");
    Check(ContainsName(*root, L"photo.jpg") && ContainsName(*root, L"icon.png"),
          "Image keeps matching files at both hierarchy levels");
    Check(!ContainsName(*root, L"movie.mp4") && !ContainsName(*root, L"archive.zip"),
          "Image removes non-image files");

    const auto child = std::find_if(root->children.begin(), root->children.end(),
                                    [](const std::unique_ptr<ffui::TreemapNode>& node) {
                                        return node->name == L"Child";
                                    });
    Check(child != root->children.end() && (*child)->totalSizeBytes == 25,
          "nested folder aggregate contains only matching descendants");
    Check(root->totalSizeBytes == 125, "root aggregate contains only matching descendants");
}

void TestDifferentCategoryRemovesThePreviousCategory() {
    auto root = BuildFixture();
    Apply(*root, L"video");

    Check(root->children.size() == 1 && ContainsName(*root, L"movie.mp4"),
          "Video keeps only the video fixture file");
    Check(!ContainsName(*root, L"photo.jpg") && !ContainsName(*root, L"icon.png"),
          "switching category removes the previous image nodes");
    Check(root->totalSizeBytes == 400, "Video aggregate excludes image and archive bytes");
}

void TestEmptyCategoryClearsTheModel() {
    auto root = BuildFixture();
    Apply(*root, L"game");

    Check(root->children.empty(), "empty category produces no stale Treemap children");
    Check(root->totalSizeBytes == 0, "empty category produces a zero aggregate");
}

void TestClassifierIsSharedWithTheFilter() {
    ffui::CategoryEngine engine;
    const auto image = engine.Match(L"PHOTO.JPG");

    Check(image.matched && image.categoryId == L"image", "classifier is case-insensitive for image files");
    Check(engine.Matches(L"PHOTO.JPG", L"image"), "Treemap filter uses the canonical image classifier");
    Check(!engine.Matches(L"movie.mp4", L"image"), "canonical classifier rejects a different category");
    Check(engine.Matches(L"README", L"other"), "canonical fallback classifier recognizes Other");
}

void TestRepeatedTransitionsAreDeterministic() {
    auto all = BuildFixture();
    auto image = BuildFixture();
    auto video = BuildFixture();
    auto allAgain = BuildFixture();
    Apply(*all, L"");
    Apply(*image, L"image");
    Apply(*video, L"video");
    Apply(*allAgain, L"");

    Check(all->totalSizeBytes == 775 && image->totalSizeBytes == 125 &&
              video->totalSizeBytes == 400 && allAgain->totalSizeBytes == 775,
          "All to Image to Video to All restores deterministic aggregates");
    Check(all->children.size() == allAgain->children.size() &&
              CountFiles(*all) == CountFiles(*allAgain),
          "All to Image to Video to All restores the original model shape");

    auto repeated = BuildFixture();
    Apply(*repeated, L"image");
    Apply(*repeated, L"image");
    Check(repeated->totalSizeBytes == 125 && CountFiles(*repeated) == 2,
          "repeated application of one category is deterministic");
}

} // namespace

int main() {
    TestAllPreservesTheFullTree();
    TestImageKeepsNestedContainersAndReaggregates();
    TestDifferentCategoryRemovesThePreviousCategory();
    TestEmptyCategoryClearsTheModel();
    TestClassifierIsSharedWithTheFilter();
    TestRepeatedTransitionsAreDeterministic();
    return fftest::FailureCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
