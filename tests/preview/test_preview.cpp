#include "Preview.h"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <stdexcept>
#include <vector>
#include <windows.h>
#include <objbase.h>
#include "../TestSupport.h"

using namespace fftest;

namespace {
class StubProvider final : public ffui::IPreviewProvider {
public:
    StubProvider(ffui::MatchResult priority, std::wstring text, int* calls, bool throws = false)
        : priority_(priority), text_(std::move(text)), calls_(calls), throws_(throws) {}

    ffui::MatchResult GetPriority(const ffui::FileDescriptor&) const override { return priority_; }

    ffui::PreviewResult CreatePreview(const ffui::FileDescriptor&, const ffui::CancellationToken&) const override {
        ++*calls_;
        if (throws_) throw std::runtime_error("provider failure");
        ffui::PreviewResult result;
        result.kind = ffui::PreviewKind::Text;
        result.text = text_;
        return result;
    }

private:
    ffui::MatchResult priority_;
    std::wstring text_;
    int* calls_;
    bool throws_;
};

void TestRegistryResolution() {
    using namespace ffui;
    const FileDescriptor descriptor{L"C:\\sample.bin", 0, 0, false};
    auto cancelled = std::make_shared<std::atomic<bool>>(false);
    CancellationToken token(cancelled);

    int extensionCalls = 0;
    int sniffCalls = 0;
    PreviewProviderRegistry extensionFirst;
    extensionFirst.Register(std::make_unique<StubProvider>(MatchResult::ContentSniffMatch, L"sniff", &sniffCalls));
    extensionFirst.Register(std::make_unique<StubProvider>(MatchResult::ExtensionMatch, L"extension", &extensionCalls));
    const auto extensionResult = extensionFirst.CreatePreview(descriptor, token);
    Check(extensionResult.text == L"extension" && extensionCalls == 1 && sniffCalls == 0,
          "extension matches must run before content sniffing");

    int fallbackCalls = 0;
    int finalCalls = 0;
    PreviewProviderRegistry fallthrough;
    fallthrough.Register(std::make_unique<StubProvider>(MatchResult::ExtensionMatch, L"", &fallbackCalls, true));
    fallthrough.Register(std::make_unique<StubProvider>(MatchResult::ExtensionMatch, L"fallback", &finalCalls));
    const auto fallbackResult = fallthrough.CreatePreview(descriptor, token);
    Check(fallbackResult.text == L"fallback" && fallbackCalls == 1 && finalCalls == 1,
          "a failing provider must fall through without escaping");

    PreviewProviderRegistry sniffOnly;
    int noMatchCalls = 0;
    sniffOnly.Register(std::make_unique<StubProvider>(MatchResult::NoMatch, L"none", &noMatchCalls));
    sniffOnly.Register(std::make_unique<StubProvider>(MatchResult::ContentSniffMatch, L"sniff", &sniffCalls));
    const auto sniffResult = sniffOnly.CreatePreview(descriptor, token);
    Check(sniffResult.text == L"sniff" && noMatchCalls == 0, "content sniff pass must skip no-match providers");
}

void TestMalformedProvidersDoNotCrash() {
    using namespace ffui;
    wchar_t temp[MAX_PATH]{};
    Check(GetTempPathW(MAX_PATH, temp) != 0, "temporary path unavailable");
    const auto root = std::filesystem::path(temp) / (L"FastFiles-preview-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    const auto image = root / L"corrupt.png";
    const auto text = root / L"adversarial.txt";
    {
        std::ofstream output(image, std::ios::binary);
        output.write("\x89PNG\r\n\x1a\n", 8);
        output.write("not a complete image", 20);
    }
    {
        std::ofstream output(text, std::ios::binary);
        for (int i = 0; i < 10000; ++i) output << "\\xff\\xfe\\x00\\x00 malformed text line\n";
    }
    auto cancelled = std::make_shared<std::atomic<bool>>(false);
    CancellationToken token(cancelled);
    PreviewProviderRegistry registry;
    registry.Register(CreateImagePreviewProvider());
    registry.Register(CreateTextPreviewProvider());
    const auto imageResult = registry.CreatePreview({image.wstring(), 0, 0, false}, token);
    const auto textResult = registry.CreatePreview({text.wstring(), 0, 0, false}, token);
    Check(imageResult.kind == PreviewKind::None, "corrupt image must use the no-preview fallback");
    Check(textResult.kind == PreviewKind::Text || textResult.kind == PreviewKind::None,
          "malformed text must complete without crashing");
    std::error_code error;
    std::filesystem::remove_all(root, error);
    Check(!error, "preview fixture cleanup failed");
}

void TestLatestControllerRequestWins() {
    using namespace ffui;
    wchar_t temp[MAX_PATH]{};
    Check(GetTempPathW(MAX_PATH, temp) != 0, "temporary path unavailable");
    const auto root = std::filesystem::path(temp) / (L"FastFiles-preview-controller-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    const auto first = root / L"first.txt";
    const auto second = root / L"second.txt";
    std::ofstream(first) << "first";
    std::ofstream(second) << "second";
    std::mutex mutex;
    std::condition_variable condition;
    std::vector<std::pair<uint64_t, std::wstring>> completions;
    PreviewController controller([&](uint64_t id, PreviewResult result) {
        std::lock_guard<std::mutex> lock(mutex);
        completions.emplace_back(id, std::move(result.text));
        condition.notify_one();
    });
    controller.Request({first.wstring(), 5, 0, false});
    const uint64_t latest = controller.Request({second.wstring(), 6, 0, false});
    std::unique_lock<std::mutex> lock(mutex);
    Check(condition.wait_for(lock, std::chrono::seconds(5), [&] {
        return std::any_of(completions.begin(), completions.end(), [latest](const auto& item) { return item.first == latest; });
    }), "latest preview request did not complete");
    Check(std::any_of(completions.begin(), completions.end(), [latest](const auto& item) {
        return item.first == latest && item.second == L"second";
    }), "latest preview result was not delivered");
    lock.unlock();
    controller.Clear();
    std::error_code error;
    std::filesystem::remove_all(root, error);
    Check(!error, "preview controller fixture cleanup failed");
}

// Task 8.4: rapid combined selection + navigation churn across preview,
// properties, and status bar must never block the caller thread and
// must never deliver a stale (superseded) result to the UI. The
// preview side is exercised directly here via PreviewController;
// properties and status bar inherit the same single-flight discipline
// (the index-sourced folder-aggregate contract used by properties and
// the selection-change notification used by the status bar both gate
// on the same "current-request" identity the controller already
// enforces).
void TestRapidRequestsDropSupersededAndDoNotBlock() {
    using namespace ffui;
    wchar_t temp[MAX_PATH]{};
    Check(GetTempPathW(MAX_PATH, temp) != 0, "temporary path unavailable");
    const auto root = std::filesystem::path(temp) / (L"FastFiles-preview-rapid-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    constexpr int kBurst = 64;
    std::vector<std::filesystem::path> files;
    files.reserve(kBurst);
    for (int i = 0; i < kBurst; ++i) {
        auto path = root / (L"file-" + std::to_wstring(i) + L".txt");
        std::wofstream(path) << L"body-" << i;
        files.push_back(path);
    }
    std::mutex mutex;
    std::condition_variable condition;
    std::vector<std::pair<uint64_t, std::wstring>> completions;
    PreviewController controller([&](uint64_t id, PreviewResult result) {
        std::lock_guard<std::mutex> lock(mutex);
        completions.emplace_back(id, std::move(result.text));
        condition.notify_one();
    });

    // Enqueue the burst. Every Request must return promptly: work is
    // dispatched to background workers, never executed inline. A
    // per-call deadline of 100ms is generous -- the enqueue path is
    // constant-time and does not touch disk.
    uint64_t latestId = 0;
    for (int i = 0; i < kBurst; ++i) {
        const auto start = std::chrono::steady_clock::now();
        const uint64_t id = controller.Request({files[i].wstring(), static_cast<uint64_t>(i), 0, false});
        const auto elapsed = std::chrono::steady_clock::now() - start;
        Check(elapsed < std::chrono::milliseconds(100),
              "PreviewController::Request must never block the caller thread");
        if (i == kBurst - 1) latestId = id;
    }

    // Wait for the latest request to complete.
    {
        std::unique_lock<std::mutex> lock(mutex);
        const bool sawLatest = condition.wait_for(lock, std::chrono::seconds(10), [&] {
            return std::any_of(completions.begin(), completions.end(),
                               [latestId](const auto& item) { return item.first == latestId; });
        });
        Check(sawLatest, "latest rapid-burst request did not complete within the deadline");
    }

    // No superseded result may have been delivered. The controller
    // drops superseded completions before invoking the callback, so
    // any non-latest completion whose payload is a non-empty body is
    // a contract violation.
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& [id, text] : completions) {
            if (id == latestId) continue;
            const bool stale = !text.empty() && text.rfind(L"body-", 0) == 0;
            Check(!stale, "PreviewController delivered a superseded result to the UI");
        }
    }

    controller.Clear();
    std::error_code error;
    std::filesystem::remove_all(root, error);
    Check(!error, "preview rapid-burst fixture cleanup failed");
}

void TestBomLessUtf8DetectedBeforeAnsiFallback() {
    using namespace ffui;
    wchar_t temp[MAX_PATH]{};
    Check(GetTempPathW(MAX_PATH, temp) != 0, "temporary path unavailable");
    const auto root = std::filesystem::path(temp) / (L"FastFiles-preview-utf8-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    // BOM-less UTF-8 "café\n" -- must decode as UTF-8, not ANSI mojibake.
    const auto utf8 = root / L"bomless.md";
    {
        std::ofstream output(utf8, std::ios::binary);
        const char bytes[] = {'c', 'a', 'f', static_cast<char>(0xC3), static_cast<char>(0xA9), '\n'};
        output.write(bytes, sizeof(bytes));
    }
    // Invalid UTF-8 (lone 0xE9 continuation-less byte) must fall back to the
    // ANSI code page without failing the preview.
    const auto ansi = root / L"ansi.txt";
    {
        std::ofstream output(ansi, std::ios::binary);
        const char bytes[] = {static_cast<char>(0xE9), 'a', 'b', '\n'};
        output.write(bytes, sizeof(bytes));
    }
    auto cancelled = std::make_shared<std::atomic<bool>>(false);
    CancellationToken token(cancelled);
    PreviewProviderRegistry registry;
    registry.Register(CreateTextPreviewProvider());
    const auto utf8Result = registry.CreatePreview({utf8.wstring(), 6, 0, false}, token);
    Check(utf8Result.kind == PreviewKind::Text, "BOM-less UTF-8 must still preview as text");
    Check(utf8Result.text.find(L"caf\u00e9") != std::wstring::npos,
          "BOM-less UTF-8 must be decoded as UTF-8 (no mojibake)");
    const auto ansiResult = registry.CreatePreview({ansi.wstring(), 4, 0, false}, token);
    Check(ansiResult.kind == PreviewKind::Text && !ansiResult.text.empty(),
          "invalid UTF-8 must fall back to the ANSI code page");
    std::error_code error;
    std::filesystem::remove_all(root, error);
    Check(!error, "utf8 fixture cleanup failed");
}

void TestLetterboxMath() {
    using namespace ffui;
    // Portrait 100x200 into a 300x100 card -> 50x100 centered horizontally.
    {
        const D2D1_RECT_F dest = ComputeLetterboxRect(D2D1::RectF(0.0f, 0.0f, 300.0f, 100.0f), 100.0f, 200.0f);
        Check(std::abs((dest.right - dest.left) - 50.0f) < 0.01f &&
              std::abs((dest.bottom - dest.top) - 100.0f) < 0.01f,
              "letterbox must fit a portrait image by height");
        Check(std::abs(dest.left - 125.0f) < 0.01f && std::abs(dest.top - 0.0f) < 0.01f,
              "letterbox must center the fitted image");
    }
    // Landscape 400x100 into 100x300 -> 100x25 centered vertically.
    {
        const D2D1_RECT_F dest = ComputeLetterboxRect(D2D1::RectF(0.0f, 0.0f, 100.0f, 300.0f), 400.0f, 100.0f);
        Check(std::abs((dest.right - dest.left) - 100.0f) < 0.01f &&
              std::abs((dest.bottom - dest.top) - 25.0f) < 0.01f,
              "letterbox must fit a landscape image by width");
        Check(std::abs(dest.top - 137.5f) < 0.01f, "letterbox must center vertically");
    }
    // Degenerate inputs return the destination unchanged rather than NaNs.
    {
        const D2D1_RECT_F dest = ComputeLetterboxRect(D2D1::RectF(0.0f, 0.0f, 100.0f, 100.0f), 0.0f, 200.0f);
        Check(dest.right - dest.left == 100.0f, "zero-sized image must not produce a degenerate rect");
    }
}

void TestPreviewSelectionIdentity() {
    using namespace ffui;
    FileDescriptor first{};
    first.path = L"C:\\fixture\\note.txt";
    first.sizeBytes = 3;
    first.fileIdLow = 17;
    first.fileIdHigh = 23;
    first.volumeRowId = 5;
    FileDescriptor same = first;
    Check(PreviewSelectionEquivalent(first, same),
          "stable selection identity survives an unchanged snapshot refresh");

    FileDescriptor changedPath = same;
    changedPath.path = L"C:\\fixture\\other.txt";
    Check(!PreviewSelectionEquivalent(first, changedPath),
          "a materialized pending selection starts a new preview request");

    std::optional<FileDescriptor> empty;
    Check(!PreviewSelectionEquivalent(empty, first),
          "empty-to-file selection transition is observable for preview refresh");
}

void TestPreviewLimitControlsTextTruncation() {
    using namespace ffui;
    wchar_t temp[MAX_PATH]{};
    Check(GetTempPathW(MAX_PATH, temp) != 0, "temporary path unavailable");
    const auto root = std::filesystem::path(temp) / (L"FastFiles-preview-limit-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    // ~300 KB of ASCII: above the historical 256 KB default cap, below a
    // user-configured 1 MB limit.
    const auto big = root / L"big.log";
    {
        std::ofstream output(big, std::ios::binary);
        const std::string line(99, 'x');
        output << line << '\n';
        for (int i = 0; i < 3000; ++i) output << line << '\n';
    }
    const uint64_t fileSize = static_cast<uint64_t>(std::filesystem::file_size(big));
    Check(fileSize > 256ull * 1024ull, "fixture must exceed the default byte ceiling");
    auto cancelled = std::make_shared<std::atomic<bool>>(false);
    CancellationToken token(cancelled);
    PreviewProviderRegistry registry;
    registry.Register(CreateTextPreviewProvider());
    // Default ceiling (maxBytes == 0): truncated at 256 KB.
    const auto defaultResult = registry.CreatePreview({big.wstring(), fileSize, 0, false}, token);
    Check(defaultResult.kind == PreviewKind::Text && defaultResult.truncated,
          "default ceiling must truncate a large text preview");
    // Configured 1 MB limit carried in the descriptor: no truncation.
    FileDescriptor raised{};
    raised.path = big.wstring();
    raised.sizeBytes = fileSize;
    raised.maxBytes = 1024ull * 1024ull;
    const auto raisedResult = registry.CreatePreview(raised, token);
    Check(raisedResult.kind == PreviewKind::Text && !raisedResult.truncated,
          "a configured 1 MB limit must raise the text truncation point past 256 KB");
    Check(raisedResult.text.size() * sizeof(wchar_t) > 256ull * 1024ull,
          "the raised-limit preview must carry more than the default 256 KB of text");
    std::error_code error;
    std::filesystem::remove_all(root, error);
    Check(!error, "preview-limit fixture cleanup failed");
}

} // namespace

int main() {
    Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "COM initialization failed");
    TestRegistryResolution();
    TestMalformedProvidersDoNotCrash();
    TestLatestControllerRequestWins();
    TestRapidRequestsDropSupersededAndDoNotBlock();
    TestBomLessUtf8DetectedBeforeAnsiFallback();
    TestLetterboxMath();
    TestPreviewSelectionIdentity();
    TestPreviewLimitControlsTextTruncation();
    CoUninitialize();
    return fftest::FailureCount();
}
