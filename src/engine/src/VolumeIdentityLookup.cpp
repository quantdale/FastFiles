#include "VolumeIdentityLookup.h"

#include <windows.h>

namespace ffengine {

std::optional<DurableVolumeIdentity> ResolveDurableVolumeIdentity(wchar_t driveLetter) {
    wchar_t rootPath[] = {driveLetter, L':', L'\\', L'\0'};

    // Yields "\\?\Volume{guid}\" -- stable across drive-letter
    // reassignment, which is exactly why design.md D6 keys durable volume
    // identity off this instead of the letter itself.
    wchar_t volumeNameBuffer[64] = {};
    if (!GetVolumeNameForVolumeMountPointW(rootPath, volumeNameBuffer, static_cast<DWORD>(std::size(volumeNameBuffer)))) {
        return std::nullopt;
    }

    DWORD serialNumber = 0;
    if (!GetVolumeInformationW(rootPath, nullptr, 0, &serialNumber, nullptr, nullptr, nullptr, 0)) {
        return std::nullopt;
    }

    DurableVolumeIdentity identity;
    identity.volumeGuid = volumeNameBuffer;
    identity.serialNumber = serialNumber;
    return identity;
}

} // namespace ffengine
