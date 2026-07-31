#pragma once
#include <optional>

#include "ffindexstore/Identity.h"

namespace ffengine {

// index-storage-and-scanning tasks.md 7.1: resolves a drive letter to the
// durable volume identity (volume GUID + cached serial number,
// design.md D6) the engine uses to key persisted volume state -- distinct
// from the wire protocol's ephemeral, connection-scoped VolumeId, which
// the engine re-maps to this on every EnumerateVolumes response. Returns
// std::nullopt if the drive letter doesn't currently resolve to a mounted
// volume (a race with disconnection, or an invalid input).
std::optional<ffindexstore::VolumeKey> ResolveVolumeKeyForDriveLetter(wchar_t driveLetter);

// The reverse lookup: finds which currently-mounted drive letter (if any)
// corresponds to a durable volume identity. Used at startup to publish a
// rebuilt volume's snapshot under its current drive letter before the
// privileged connection's own EnumerateVolumes poll has run yet. Returns
// L'\0' if no currently-mounted drive matches (e.g. the volume is not
// attached right now).
wchar_t ResolveDriveLetterForVolumeKey(const ffindexstore::VolumeKey& key);

} // namespace ffengine
