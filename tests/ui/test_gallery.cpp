#include "Gallery.h"

#include "../TestSupport.h"

#include <string>

namespace {

void TestGallerySections() {
    auto sections = ffui::GallerySections();
    fftest::Check(sections.size() >= 10, "gallery exposes at least 10 component sections");

    bool hasButtons = false;
    bool hasChips = false;
    bool hasNav = false;
    bool hasProgress = false;
    bool hasRows = false;
    bool hasDrop = false;
    bool hasPanel = false;
    bool hasIconButtons = false;
    bool hasTabs = false;
    bool hasSectionHeaders = false;

    for (const auto& sec : sections) {
        fftest::Check(!sec.title.empty(), "gallery section has a title");
        fftest::Check(!sec.description.empty(), "gallery section has a description");
        fftest::Check(!sec.states.empty(), "gallery section lists states");

        if (sec.title.find(L"Buttons") != std::wstring::npos && sec.title.find(L"Icon") == std::wstring::npos) hasButtons = true;
        if (sec.title.find(L"Chips") != std::wstring::npos) hasChips = true;
        if (sec.title.find(L"Navigation") != std::wstring::npos) hasNav = true;
        if (sec.title.find(L"Progress") != std::wstring::npos) hasProgress = true;
        if (sec.title.find(L"Rows") != std::wstring::npos) hasRows = true;
        if (sec.title.find(L"Drop") != std::wstring::npos) hasDrop = true;
        if (sec.title.find(L"Panel") != std::wstring::npos) hasPanel = true;
        if (sec.title.find(L"Icon Buttons") != std::wstring::npos) hasIconButtons = true;
        if (sec.title.find(L"Tabs") != std::wstring::npos) hasTabs = true;
        if (sec.title.find(L"Section Headers") != std::wstring::npos) hasSectionHeaders = true;
    }

    fftest::Check(hasButtons, "gallery covers buttons");
    fftest::Check(hasChips, "gallery covers status chips");
    fftest::Check(hasNav, "gallery covers navigation");
    fftest::Check(hasProgress, "gallery covers progress");
    fftest::Check(hasRows, "gallery covers rows and selection");
    fftest::Check(hasDrop, "gallery covers drop target");
    fftest::Check(hasPanel, "gallery covers state panels");
    fftest::Check(hasIconButtons, "gallery covers the icon-button family and focus states");
    fftest::Check(hasTabs, "gallery covers tabs, breadcrumb, and fields");
    fftest::Check(hasSectionHeaders, "gallery covers section headers and dividers");
}

void TestGallerySizeVariants() {
    auto variants = ffui::GallerySizeVariants();
    fftest::Check(variants.size() == 3, "gallery exposes narrow/normal/wide variants");

    fftest::Check(variants[0].width >= 800.0f, "narrow variant is at least 800px");
    fftest::Check(variants[1].width >= 1280.0f, "normal variant is at least 1280px");
    fftest::Check(variants[2].width >= 1600.0f, "wide variant is at least 1600px");

    for (const auto& v : variants) {
        fftest::Check(!v.label.empty(), "size variant has a label");
        fftest::Check(v.width > 0.0f, "size variant width is positive");
        fftest::Check(v.height > 0.0f, "size variant height is positive");
    }
}

void TestGalleryGating() {
    // Without command-line args, gallery is not requested.
    fftest::Check(ffui::IsGalleryRequested() == false, "gallery not requested without flag");
    // Without --gallery (or in release builds), gallery is never allowed.
    fftest::Check(ffui::IsGalleryAllowed() == false, "gallery not allowed without flag");
}

void TestGalleryHitTest() {
    const D2D1_RECT_F clientRect = D2D1::RectF(0.0f, 0.0f, 1280.0f, 800.0f);

    fftest::Check(ffui::GalleryHitTest(D2D1::Point2F(-10.0f, 100.0f), clientRect) == -1,
                  "hit test rejects left of client rect");
    fftest::Check(ffui::GalleryHitTest(D2D1::Point2F(100.0f, -10.0f), clientRect) == -1,
                  "hit test rejects above client rect");
    fftest::Check(ffui::GalleryHitTest(D2D1::Point2F(1300.0f, 100.0f), clientRect) == -1,
                  "hit test rejects right of client rect");
    fftest::Check(ffui::GalleryHitTest(D2D1::Point2F(100.0f, 900.0f), clientRect) == -1,
                  "hit test rejects below client rect");
    fftest::Check(ffui::GalleryHitTest(D2D1::Point2F(100.0f, 50.0f), clientRect) == -1,
                  "hit test rejects header area");
    fftest::Check(ffui::GalleryHitTest(D2D1::Point2F(100.0f, 120.0f), clientRect) == 0,
                  "hit test finds buttons section in column 1");
    fftest::Check(ffui::GalleryHitTest(D2D1::Point2F(800.0f, 150.0f), clientRect) == 4,
                  "hit test finds rows section in column 2");

    const D2D1_RECT_F narrowRect = D2D1::RectF(0.0f, 0.0f, 800.0f, 720.0f);
    fftest::Check(ffui::GalleryHitTest(D2D1::Point2F(100.0f, 120.0f), narrowRect) == 0,
                  "hit test finds buttons section in narrow single column");
}

}  // namespace

int main() {
    TestGallerySections();
    TestGallerySizeVariants();
    TestGalleryGating();
    TestGalleryHitTest();
    return fftest::FailureCount();
}
