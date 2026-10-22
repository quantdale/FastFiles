// durable-cold-search-index-and-hybrid-residency 3.1: shared fold
// classification for candidate-index side sets. This header is INTERNAL to
// ffindexstore (not installed) and is the single source of truth for the
// question "does this name require conservative candidate inclusion?".
//
// Semantics (must stay identical to the in-memory accelerator's contract,
// Projection::FilenameCandidateIndices + the P2-A repair):
//   A name requires the side-set iff it carries
//     (a) any surrogate UTF-16 unit (the FTS trigram tokenizer works on
//         Unicode codepoints while canonical matching works on UTF-16 units,
//         so astral characters split differently), or
//     (b) any non-ASCII UTF-16 unit that the HOST ordinal uppercase table
//         equates with some ASCII unit (the fold-exceptional class, e.g.
//         U+017F on tables where it uppercases to 'S').
// Classification is derived from the HOST table via CompareStringOrdinal,
// memoized per unit, and FAILS CLOSED: any probe failure classifies the unit
// exceptional. An ASCII-only name can never need the side-set.
#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>

namespace ffindexstore::foldclassification {

// 0 = unprobed, 1 = non-exceptional, 2 = exceptional. Fail-closed default for
// unprobed units is handled by probing before use; a failed probe stores 2.
inline std::atomic<uint8_t>* FoldProbeCache() {
    static std::atomic<uint8_t> cache[65536] = {};
    return cache;
}

// True when this single non-ASCII unit may fold-equal some ASCII unit on the
// host table. Surrogates are classified by the caller (they are structural).
inline bool UnitMayFoldToAscii(unsigned short unit) {
    if (unit <= 0x7F) return false;
    if (unit >= 0xD800 && unit <= 0xDFFF) return true;
    std::atomic<uint8_t>* cache = FoldProbeCache();
    uint8_t verdict = cache[unit].load(std::memory_order_acquire);
    if (verdict != 0) return verdict == 2;
    // Exhaustive 128-ASCII probe with the same call the matcher uses.
    bool exceptional = false;
    const wchar_t wide = static_cast<wchar_t>(unit);
    for (unsigned short ascii = 0; ascii < 0x80; ++ascii) {
        const wchar_t asciiUnit = static_cast<wchar_t>(ascii);
        const int result = CompareStringOrdinal(&wide, 1, &asciiUnit, 1, TRUE);
        if (result == 0) {
            // Probe failure: fail closed (classify exceptional).
            exceptional = true;
            break;
        }
        if (result == CSTR_EQUAL) {
            exceptional = true;
            break;
        }
    }
    cache[unit].store(exceptional ? 2 : 1, std::memory_order_release);
    return exceptional;
}

// True when the name (UTF-16 units) requires conservative candidate
// inclusion.
inline bool NameRequiresCandidateSideSet(const char16_t* name, size_t length) {
    for (size_t index = 0; index < length; ++index) {
        if (UnitMayFoldToAscii(static_cast<unsigned short>(name[index]))) return true;
    }
    return false;
}

// --- testing seam (durable-cold-search-index-and-hybrid-residency 4.1) ----
// Mirrors Projection::SetTestingFoldExceptionalUnit: lets tests simulate a
// host ordinal table where specific units fold to ASCII (the fold-exceptional
// class), so the side-set rule can be exercised red-first on any host. The
// seam writes THROUGH to the memoized probe cache (the classification reads
// only that cache), so an injected verdict replaces the host probe for that
// unit. NOTE (reviewer-A P3, corrected comment): injecting exceptional=false
// forces the unit NON-exceptional and therefore OVERRIDES a genuine host
// probe -- it is a simulation control, not a fail-closed operation.
// Production code never injects.
inline void SetTestingFoldExceptionalUnit(unsigned short unit, bool exceptional) {
    FoldProbeCache()[unit].store(exceptional ? 2 : 1, std::memory_order_release);
}

inline void ResetTestingFoldExceptionalUnits() {
    std::atomic<uint8_t>* cache = FoldProbeCache();
    for (unsigned int unit = 0; unit < 65536; ++unit) {
        cache[unit].store(0, std::memory_order_release);
    }
}

// Host fold signature (lane-B P1, durable-cold-search 4.x): the persisted
// side-set classification is HOST-BOUND. A database written on one host must
// not serve candidates on a host whose ordinal table differs (a stale
// classification would under-admit candidates = false negatives). The
// signature is an exhaustive probe of every non-ASCII BMP unit against the
// 128 ASCII units (the same CompareStringOrdinal calls the classification
// uses), hashed with FNV-1a. Computed at most once per process (cached);
// ~8.4M cheap API calls on a cold process, free afterwards. A mismatching
// stored signature forces a candidate-index rebuild at open.
inline uint64_t HostFoldSignature() {
    static uint64_t signature = []() -> uint64_t {
        std::atomic<uint8_t>* cache = FoldProbeCache();
        for (unsigned int unit = 0x80; unit < 0x10000; ++unit) {
            if (unit >= 0xD800 && unit <= 0xDFFF) continue;  // structural, host-independent
            (void)UnitMayFoldToAscii(static_cast<unsigned short>(unit));
        }
        uint64_t hash = 1469598103934665603ull;
        for (unsigned int unit = 0; unit < 0x10000; ++unit) {
            const uint8_t verdict = cache[unit].load(std::memory_order_acquire);
            hash ^= verdict;
            hash *= 1099511628211ull;
        }
        return hash;
    }();
    return signature;
}

}  // namespace ffindexstore::foldclassification
