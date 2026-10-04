#pragma once
// performance-closure-and-multisurface-acceleration 8.1: benchmark-scoped
// allocator probe used by the projection-memory decomposition.
//
// Why this exists: the campaign must account for a memory decomposition with
// allocation evidence, not only a `sizeof` estimate. This probe records, for
// every C++ allocation in the hosting benchmark process:
//   * requested bytes and the allocator's usable block bytes (`_msize`),
//   * live requested/usable bytes, live and cumulative allocation counts,
//   * a live requested-bytes high-water mark.
//
// Measurement boundary (stated, not hidden):
//   * Only C++ `operator new`/`operator delete` traffic is counted. SQLite is
//     compiled as C and allocates through `malloc`/`free` directly, so the
//     durable store's pages and per-connection bookkeeping are deliberately
//     outside this counter; they show up in the process-private reconciliation
//     instead of being invented here.
//   * A 32-byte header stores the base pointer and the requested/usable sizes.
//     `usable` is the caller-usable size (the probe header excluded on both
//     allocation paths), so `usable - requested` is pure heap rounding; the
//     probe header itself and the heap block header stay residual overhead and
//     are never attributed to a projection category.
//   * Overriding the replaceable global operators makes this an instrumented
//     executable: its numbers are allocation evidence, not wall-clock
//     evidence.
//
// Usage: define FFBENCH_ALLOCATOR_PROBE_DEFINE in exactly one translation unit
// per executable before including this header; other translation units may
// include it without the define to read snapshots.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <malloc.h>
#include <new>

namespace ffbench {

// MSVC x64's default `operator new` alignment is 16, which is also
// `alignof(std::max_align_t)` there. It is spelled locally rather than via the
// implementation macro so no reserved identifier appears in this file; the
// static_assert below pins the relationship the probe relies on.
inline constexpr std::size_t kDefaultNewAlignment = 16;
static_assert(alignof(std::max_align_t) <= kDefaultNewAlignment,
              "the probe's default alignment must satisfy max_align_t");

// Constant-initialized counters: no dynamic initialization runs before the
// replaced operators become active, so early allocations are still counted.
inline std::atomic<unsigned long long> gLiveRequestedBytes{0};
inline std::atomic<unsigned long long> gLiveUsableBytes{0};
inline std::atomic<unsigned long long> gLiveAllocations{0};
inline std::atomic<unsigned long long> gTotalAllocations{0};
inline std::atomic<unsigned long long> gTotalRequestedBytes{0};
inline std::atomic<unsigned long long> gPeakLiveRequestedBytes{0};

struct AllocatorSnapshot {
    unsigned long long liveRequestedBytes = 0;
    unsigned long long liveUsableBytes = 0;
    unsigned long long liveAllocations = 0;
    unsigned long long totalAllocations = 0;
    unsigned long long totalRequestedBytes = 0;
    unsigned long long peakLiveRequestedBytes = 0;

    // Live allocator overhead beyond the requested payload (heap block
    // header, alignment/rounding, and this probe's own header). Positive
    // values are rounding overhead, not a category.
    unsigned long long liveRoundingOverheadBytes() const noexcept {
        return liveUsableBytes > liveRequestedBytes ? liveUsableBytes - liveRequestedBytes : 0;
    }
};

inline AllocatorSnapshot ReadAllocatorSnapshot() noexcept {
    AllocatorSnapshot snapshot;
    snapshot.liveRequestedBytes = gLiveRequestedBytes.load(std::memory_order_relaxed);
    snapshot.liveUsableBytes = gLiveUsableBytes.load(std::memory_order_relaxed);
    snapshot.liveAllocations = gLiveAllocations.load(std::memory_order_relaxed);
    snapshot.totalAllocations = gTotalAllocations.load(std::memory_order_relaxed);
    snapshot.totalRequestedBytes = gTotalRequestedBytes.load(std::memory_order_relaxed);
    snapshot.peakLiveRequestedBytes = gPeakLiveRequestedBytes.load(std::memory_order_relaxed);
    return snapshot;
}

// Live-bytes delta between two snapshots taken in the same process. A
// negative result (a phase releasing more than it allocated) is reported as
// signed arithmetic by the caller, so this returns a signed difference.
inline long long LiveRequestedDelta(const AllocatorSnapshot& before,
                                    const AllocatorSnapshot& after) noexcept {
    return static_cast<long long>(after.liveRequestedBytes)
        - static_cast<long long>(before.liveRequestedBytes);
}

inline unsigned long long LiveUsableDelta(const AllocatorSnapshot& before,
                                          const AllocatorSnapshot& after) noexcept {
    return after.liveUsableBytes - before.liveUsableBytes;
}

inline unsigned long long AllocationsDelta(const AllocatorSnapshot& before,
                                           const AllocatorSnapshot& after) noexcept {
    return after.totalAllocations - before.totalAllocations;
}

inline unsigned long long RequestedBytesDelta(const AllocatorSnapshot& before,
                                              const AllocatorSnapshot& after) noexcept {
    return after.totalRequestedBytes - before.totalRequestedBytes;
}

namespace detail {

struct AllocationHeader {
    void* baseForFree = nullptr;
    std::size_t requestedBytes = 0;
    std::size_t usableBytes = 0;
    std::size_t reserved = 0;
};
static_assert(sizeof(AllocationHeader) % ffbench::kDefaultNewAlignment == 0,
              "probe header must preserve the default new alignment");

inline void RecordAllocation(const std::size_t requested, const std::size_t usable) noexcept {
    gLiveRequestedBytes.fetch_add(requested, std::memory_order_relaxed);
    gLiveUsableBytes.fetch_add(usable, std::memory_order_relaxed);
    gLiveAllocations.fetch_add(1, std::memory_order_relaxed);
    gTotalAllocations.fetch_add(1, std::memory_order_relaxed);
    gTotalRequestedBytes.fetch_add(requested, std::memory_order_relaxed);
    const unsigned long long live = gLiveRequestedBytes.load(std::memory_order_relaxed);
    unsigned long long peak = gPeakLiveRequestedBytes.load(std::memory_order_relaxed);
    while (live > peak
           && !gPeakLiveRequestedBytes.compare_exchange_weak(
                  peak, live, std::memory_order_relaxed, std::memory_order_relaxed)) {
        peak = gPeakLiveRequestedBytes.load(std::memory_order_relaxed);
    }
}

inline void RecordDeallocation(const AllocationHeader& header) noexcept {
    gLiveRequestedBytes.fetch_sub(header.requestedBytes, std::memory_order_relaxed);
    gLiveUsableBytes.fetch_sub(header.usableBytes, std::memory_order_relaxed);
    gLiveAllocations.fetch_sub(1, std::memory_order_relaxed);
}

inline void* AllocateTracked(const std::size_t size, const std::size_t alignment) noexcept {
    const std::size_t requested = size == 0 ? 1 : size;
    if (alignment <= kDefaultNewAlignment) {
        void* base = std::malloc(requested + sizeof(AllocationHeader));
        if (base == nullptr) return nullptr;
        auto* header = static_cast<AllocationHeader*>(base);
        header->baseForFree = base;
        header->requestedBytes = requested;
        // Caller-usable bytes: _msize includes this probe's own header.
        header->usableBytes = _msize(base) - sizeof(AllocationHeader);
        RecordAllocation(requested, header->usableBytes);
        return static_cast<void*>(header + 1);
    }
    // Aligned path: reserve header + alignment slack, align the payload, and
    // keep the malloc base in the header so the matching delete can free it.
    void* base = std::malloc(requested + alignment + sizeof(AllocationHeader));
    if (base == nullptr) return nullptr;
    const std::uintptr_t unaligned =
        reinterpret_cast<std::uintptr_t>(base) + sizeof(AllocationHeader);
    const std::uintptr_t aligned =
        (unaligned + alignment - 1) & ~static_cast<std::uintptr_t>(alignment - 1);
    auto* header = reinterpret_cast<AllocationHeader*>(aligned) - 1;
    const std::size_t consumed = static_cast<std::size_t>(aligned - reinterpret_cast<std::uintptr_t>(base));
    const std::size_t rawUsable = _msize(base);
    header->baseForFree = base;
    header->requestedBytes = requested;
    header->usableBytes = rawUsable > consumed ? rawUsable - consumed : requested;
    RecordAllocation(requested, header->usableBytes);
    return reinterpret_cast<void*>(aligned);
}

inline void DeallocateTracked(void* memory) noexcept {
    if (memory == nullptr) return;
    auto* header = static_cast<AllocationHeader*>(memory) - 1;
    RecordDeallocation(*header);
    std::free(header->baseForFree);
}

inline void* AllocateChecked(const std::size_t size, const std::size_t alignment) {
    void* memory = AllocateTracked(size, alignment);
    if (memory == nullptr) throw std::bad_alloc{};
    return memory;
}

inline void* AllocateNoThrow(const std::size_t size, const std::size_t alignment) noexcept {
    return AllocateTracked(size, alignment);
}

}  // namespace detail
}  // namespace ffbench

#ifdef FFBENCH_ALLOCATOR_PROBE_DEFINE
// Replaceable global allocation functions. The SAL annotations mirror
// vcruntime_new.h so MSVC /analyze (C28251) accepts the replacements.
_Ret_notnull_ _Post_writable_byte_size_(size) void* operator new(std::size_t size) {
    return ffbench::detail::AllocateChecked(size, ffbench::kDefaultNewAlignment);
}
_Ret_notnull_ _Post_writable_byte_size_(size) void* operator new[](std::size_t size) {
    return ffbench::detail::AllocateChecked(size, ffbench::kDefaultNewAlignment);
}
_Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(size)
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    return ffbench::detail::AllocateNoThrow(size, ffbench::kDefaultNewAlignment);
}
_Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(size)
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    return ffbench::detail::AllocateNoThrow(size, ffbench::kDefaultNewAlignment);
}
_Ret_notnull_ _Post_writable_byte_size_(size) void* operator new(std::size_t size,
                                                                 std::align_val_t alignment) {
    return ffbench::detail::AllocateChecked(size, static_cast<std::size_t>(alignment));
}
_Ret_notnull_ _Post_writable_byte_size_(size) void* operator new[](std::size_t size,
                                                                   std::align_val_t alignment) {
    return ffbench::detail::AllocateChecked(size, static_cast<std::size_t>(alignment));
}
_Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(size)
void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    return ffbench::detail::AllocateNoThrow(size, static_cast<std::size_t>(alignment));
}
_Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(size)
void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    return ffbench::detail::AllocateNoThrow(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* memory) noexcept { ffbench::detail::DeallocateTracked(memory); }
void operator delete[](void* memory) noexcept { ffbench::detail::DeallocateTracked(memory); }
void operator delete(void* memory, std::size_t) noexcept { ffbench::detail::DeallocateTracked(memory); }
void operator delete[](void* memory, std::size_t) noexcept { ffbench::detail::DeallocateTracked(memory); }
void operator delete(void* memory, const std::nothrow_t&) noexcept {
    ffbench::detail::DeallocateTracked(memory);
}
void operator delete[](void* memory, const std::nothrow_t&) noexcept {
    ffbench::detail::DeallocateTracked(memory);
}
void operator delete(void* memory, std::align_val_t) noexcept {
    ffbench::detail::DeallocateTracked(memory);
}
void operator delete[](void* memory, std::align_val_t) noexcept {
    ffbench::detail::DeallocateTracked(memory);
}
void operator delete(void* memory, std::size_t, std::align_val_t) noexcept {
    ffbench::detail::DeallocateTracked(memory);
}
void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept {
    ffbench::detail::DeallocateTracked(memory);
}
void operator delete(void* memory, std::align_val_t, const std::nothrow_t&) noexcept {
    ffbench::detail::DeallocateTracked(memory);
}
void operator delete[](void* memory, std::align_val_t, const std::nothrow_t&) noexcept {
    ffbench::detail::DeallocateTracked(memory);
}
#endif  // FFBENCH_ALLOCATOR_PROBE_DEFINE
