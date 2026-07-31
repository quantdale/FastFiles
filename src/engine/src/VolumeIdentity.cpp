#include "VolumeIdentity.h"

#include <windows.h>
#include <rpc.h>

#include <cstring>

namespace ffengine {

std::optional<ffindexstore::VolumeKey> ResolveVolumeKeyForDriveLetter(wchar_t driveLetter) {
    const wchar_t rootPath[] = {driveLetter, L':', L'\\', L'\0'};

    wchar_t mountPointName[256];
    if (!GetVolumeNameForVolumeMountPointW(rootPath, mountPointName, static_cast<DWORD>(std::size(mountPointName)))) {
        return std::nullopt;
    }

    // mountPointName looks like L"\\?\Volume{4c1b02c1-...}\" -- extract
    // the substring between the braces for UuidFromStringW, which expects
    // "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee" without braces.
    std::wstring mountPoint(mountPointName);
    const size_t openBrace = mountPoint.find(L'{');
    const size_t closeBrace = mountPoint.find(L'}');
    if (openBrace == std::wstring::npos || closeBrace == std::wstring::npos || closeBrace <= openBrace) {
        return std::nullopt;
    }
    std::wstring guidText = mountPoint.substr(openBrace + 1, closeBrace - openBrace - 1);

    UUID uuid{};
    if (UuidFromStringW(reinterpret_cast<RPC_WSTR>(guidText.data()), &uuid) != RPC_S_OK) {
        return std::nullopt;
    }

    DWORD serialNumber = 0;
    if (!GetVolumeInformationW(rootPath, nullptr, 0, &serialNumber, nullptr, nullptr, nullptr, 0)) {
        return std::nullopt;
    }

    ffindexstore::VolumeKey key;
    static_assert(sizeof(UUID) == 16, "UUID must be exactly 16 bytes to fit VolumeKey::volumeGuid");
    std::memcpy(key.volumeGuid.data(), &uuid, sizeof(UUID));
    key.serialNumber = static_cast<uint32_t>(serialNumber);
    return key;
}

wchar_t ResolveDriveLetterForVolumeKey(const ffindexstore::VolumeKey& key) {
    const DWORD driveMask = GetLogicalDrives();
    for (wchar_t letter = L'A'; letter <= L'Z'; ++letter) {
        if ((driveMask & (1u << (letter - L'A'))) == 0) {
            continue;
        }
        auto candidate = ResolveVolumeKeyForDriveLetter(letter);
        if (candidate && *candidate == key) {
            return letter;
        }
    }
    return L'\0';
}

} // namespace ffengine
