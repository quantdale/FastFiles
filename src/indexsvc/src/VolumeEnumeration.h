#pragma once
#include <vector>

#include "ffprotocol/Commands.h"

namespace ffindexsvc {

// Enumerates only fixed local NTFS/ReFS volumes discovered by the service
// itself (spec "Volume enumeration is service-controlled") -- there is no
// way for a client to supply a path or device string to select a volume
// instead. VolumeId values are opaque and stable for the lifetime of the
// service process; a given drive letter always maps to the same VolumeId
// across repeated calls, but the mapping is not persisted across restarts
// (design.md "Stateless Operation").
std::vector<ffprotocol::VolumeInfo> EnumerateFixedNtfsVolumes();

// Resolves a previously-issued VolumeId back to its drive letter, for use
// by command handlers (e.g. StartVolumeScan) that receive only the opaque
// ID. Returns L'\0' if the ID is unknown (never issued, or the volume was
// removed since).
wchar_t ResolveVolumeIdToDriveLetter(ffprotocol::VolumeId id);

} // namespace ffindexsvc
