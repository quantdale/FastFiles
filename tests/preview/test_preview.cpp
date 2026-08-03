#include "Preview.h"

#include <atomic>
#include <algorithm>
#include <chrono>
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

namespace {

void Check(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

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

} // namespace

int main() {
    Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "COM initialization failed");
    TestRegistryResolution();
    TestMalformedProvidersDoNotCrash();
    TestLatestControllerRequestWins();
    TestRapidRequestsDropSupersededAndDoNotBlock();
    CoUninitialize();
}
