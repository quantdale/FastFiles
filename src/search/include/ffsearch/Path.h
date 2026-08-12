#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace ffsearch {

// Windows-aware path helpers shared by the UI snapshot fallback and the
// in-process search scope matcher.  They preserve drive/UNC roots, including
// extended-length (\\?\) spellings, instead of treating the root's colon or
// namespace prefix as a normal parent component.
std::wstring ParentPath(std::wstring_view path);
std::wstring LeafName(std::wstring_view path);
bool SameVolume(std::wstring_view left, std::wstring_view right);
int64_t VolumeId(std::wstring_view path);

} // namespace ffsearch
