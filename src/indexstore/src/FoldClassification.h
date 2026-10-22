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

}  // namespace ffindexstore::foldclassification
