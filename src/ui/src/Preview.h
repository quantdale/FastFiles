#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <deque>
#include <string>
#include <thread>
#include <vector>

namespace ffui {

struct FileDescriptor {
    std::wstring path;
    uint64_t sizeBytes = 0;
    uint32_t attributes = 0;
    bool isDirectory = false;
};

enum class MatchResult {
    NoMatch,
    ExtensionMatch,
    ContentSniffMatch,
};

class CancellationToken {
public:
    explicit CancellationToken(std::shared_ptr<std::atomic<bool>> cancelled) : cancelled_(std::move(cancelled)) {}
    bool IsCancelled() const noexcept { return cancelled_->load(); }

private:
    std::shared_ptr<std::atomic<bool>> cancelled_;
};

enum class PreviewKind {
    None,
    Text,
    Image,
};

struct PreviewResult {
    PreviewKind kind = PreviewKind::None;
    std::wstring text;
    std::vector<uint8_t> pixels;
    uint32_t width = 0;
    uint32_t height = 0;
    bool truncated = false;
};

class IPreviewProvider {
public:
    virtual ~IPreviewProvider() = default;
    virtual MatchResult GetPriority(const FileDescriptor& descriptor) const = 0;
    virtual PreviewResult CreatePreview(const FileDescriptor& descriptor, const CancellationToken& cancellation) const = 0;
};

class PreviewProviderRegistry {
public:
    void Register(std::unique_ptr<IPreviewProvider> provider);
    PreviewResult CreatePreview(const FileDescriptor& descriptor, const CancellationToken& cancellation) const;

private:
    std::vector<std::unique_ptr<IPreviewProvider>> providers_;
};

std::unique_ptr<IPreviewProvider> CreateImagePreviewProvider();
std::unique_ptr<IPreviewProvider> CreateTextPreviewProvider();

// Runs only the direct, unprivileged disk reads performed by preview
// providers. A later request invalidates all earlier results before they
// are delivered to the UI thread.
class PreviewController {
public:
    using Completion = std::function<void(uint64_t requestId, PreviewResult)>;

    explicit PreviewController(Completion completion);
    ~PreviewController();
    uint64_t Request(const FileDescriptor& descriptor);
    void Clear();

private:
    void WorkerLoop();

    PreviewProviderRegistry registry_;
    Completion completion_;
    std::mutex mutex_;
    std::condition_variable workAvailable_;
    std::deque<std::function<void()>> pendingWork_;
    std::vector<std::thread> workers_;
    bool stopping_ = false;
    std::shared_ptr<std::atomic<bool>> cancellation_;
    uint64_t requestId_ = 0;
};

} // namespace ffui
