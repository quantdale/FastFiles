#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace ffengine {

struct DurableVolumeIdentity {
    std::wstring volumeGuid; // e.g. "{6b29fc40-ca47-1067-b31d-00dd010662da}"
    uint64_t serialNumber = 0;
};

// Resolves a drive letter to its durable identity (design.md D6: volume
// GUID + cached serial number) via ordinary unprivileged Win32 calls
// (GetVolumeNameForVolumeMountPointW / GetVolumeInformationW) -- the
// engine does not need the privileged service's help for this, since it's
// the same information Explorer's own "volume properties" dialog reads.
// Returns std::nullopt if the drive letter doesn't currently resolve to a
// mounted volume.
std::optional<DurableVolumeIdentity> ResolveDurableVolumeIdentity(wchar_t driveLetter);

} // namespace ffengine
