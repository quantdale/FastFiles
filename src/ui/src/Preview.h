#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <d2d1.h>
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
    // allocated-size-storage-accuracy 6.3: size-on-disk with explicit known
    // state, carried for the details pane (never rendered as 0 B).
    uint64_t allocatedSizeBytes = 0;
    bool allocatedSizeKnown = false;
    // Byte ceiling a provider may read for this preview. 0 means "provider
    // default" (the historical 256 KB text cap); the shell forwards the
    // user-configured Preview Limit so the advertised control governs
    // truncation, not just whether a preview runs.
    uint64_t maxBytes = 0;
    uint32_t attributes = 0;
    bool isDirectory = false;
    uint64_t fileIdLow = 0;
    uint64_t fileIdHigh = 0;
    int64_t volumeRowId = 0;
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

// Fits an image of `width` x `height` into `dest` with a uniform scale,
// centered (letterbox/pillarbox). Never upscales beyond the destination and
// never distorts the aspect ratio. Pure math so it is headless-testable.
inline D2D1_RECT_F ComputeLetterboxRect(const D2D1_RECT_F& dest, float width, float height) {
    const float availW = (std::max)(0.0f, dest.right - dest.left);
    const float availH = (std::max)(0.0f, dest.bottom - dest.top);
    if (width <= 0.0f || height <= 0.0f || availW <= 0.0f || availH <= 0.0f) {
        return dest;
    }
    const float scale = (std::min)(availW / width, availH / height);
    const float drawW = width * scale;
    const float drawH = height * scale;
    const float left = dest.left + (availW - drawW) * 0.5f;
    const float top = dest.top + (availH - drawH) * 0.5f;
    return D2D1::RectF(left, top, left + drawW, top + drawH);
}

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
