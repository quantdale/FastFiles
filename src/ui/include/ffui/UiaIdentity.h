#pragma once
// Pure identity-hashing helpers, self-contained (no src dependencies) so the
// semantic seams in include/ffui can reuse them. This is the canonical home of
// the FNV-1a wide-string hash and the FRN-or-path identity rule;
// UiaRuntimeIds.h includes this header and adds only the UIA runtime-ID array
// builders (semantic-treemap-uia 13.x).

#include <cstdint>
#include <string>

namespace ffui {

// FNV-1a 64-bit over a wide string (ordinal, case-sensitive). Identity is
// used for equality during a provider lifetime; the underlying model is
// already normalized (column paths come from the engine, names from the
// directory listing), so case folding is not applied here.
inline uint64_t UiaHashWide(const wchar_t* text, size_t length) {
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < length; ++i) {
        hash ^= static_cast<unsigned char>(text[i] & 0xFF);
        hash *= 1099511628211ull;
        hash ^= static_cast<unsigned char>((text[i] >> 8) & 0xFF);
        hash *= 1099511628211ull;
    }
    return hash;
}

inline uint64_t UiaHashWide(const std::wstring& text) {
    return UiaHashWide(text.data(), text.size());
}

// Mix a numeric component into a running hash.
inline uint64_t UiaHashMix(uint64_t hash, uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ull;
    return hash;
}

// Split a 64-bit hash into two ints for a UIA runtime-ID array.
inline int UiaHashLow(uint64_t hash) { return static_cast<int>(hash & 0xFFFFFFFFull); }
inline int UiaHashHigh(uint64_t hash) { return static_cast<int>((hash >> 32) & 0xFFFFFFFFull); }

// Stable identity of a column item / treemap tile: indexed FRN identity when
// present (volumeRowId + fileIdLow/High nonzero), else the case-insensitive
// full path. Mirrors the selection-identity policy of the real model.
inline uint64_t UiaItemIdentityHash(int64_t volumeRowId, uint64_t fileIdLow,
                                    uint64_t fileIdHigh, const std::wstring& fullPath) {
    if (volumeRowId != 0 && (fileIdLow != 0 || fileIdHigh != 0)) {
        uint64_t hash = UiaHashWide(L"frn");
        hash = UiaHashMix(hash, static_cast<uint64_t>(volumeRowId));
        hash = UiaHashMix(hash, fileIdLow);
        hash = UiaHashMix(hash, fileIdHigh);
        return hash;
    }
    return UiaHashWide(fullPath);
}

// Stable identity of a search result: indexed (volumeId + id) when present,
// else folder + name.
inline uint64_t UiaSearchIdentityHash(int64_t volumeId, uint64_t id,
                                      const std::wstring& folder, const std::wstring& name) {
    if (volumeId != 0 && id != 0) {
        uint64_t hash = UiaHashWide(L"sid");
        hash = UiaHashMix(hash, static_cast<uint64_t>(volumeId));
        hash = UiaHashMix(hash, id);
        return hash;
    }
    uint64_t hash = UiaHashWide(folder);
    hash = UiaHashMix(hash, UiaHashWide(name));
    return hash;
}

} // namespace ffui
