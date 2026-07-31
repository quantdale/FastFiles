#include "VolumeEnumeration.h"

#include <windows.h>
#include <cwchar>
#include <iterator>
#include <map>
#include <mutex>

namespace ffindexsvc {

namespace {

std::mutex g_mutex;
std::map<wchar_t, uint32_t> g_driveLetterToId;
uint32_t g_nextId = 1;

uint32_t IdForDriveLetter(wchar_t driveLetter) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_driveLetterToId.find(driveLetter);
    if (it != g_driveLetterToId.end()) {
        return it->second;
    }
    const uint32_t id = g_nextId++;
    g_driveLetterToId.emplace(driveLetter, id);
    return id;
}

bool IsNtfsOrRefs(const wchar_t* rootPath) {
    wchar_t fileSystemName[MAX_PATH + 1] = {};
    if (!GetVolumeInformationW(rootPath, nullptr, 0, nullptr, nullptr, nullptr, fileSystemName,
                                static_cast<DWORD>(std::size(fileSystemName)))) {
        return false;
    }
    return _wcsicmp(fileSystemName, L"NTFS") == 0 || _wcsicmp(fileSystemName, L"ReFS") == 0;
}

} // namespace

std::vector<ffprotocol::VolumeInfo> EnumerateFixedNtfsVolumes() {
    std::vector<ffprotocol::VolumeInfo> volumes;

    const DWORD driveMask = GetLogicalDrives();
    for (wchar_t letter = L'A'; letter <= L'Z'; ++letter) {
        const int bit = letter - L'A';
        if ((driveMask & (1u << bit)) == 0) {
            continue;
        }

        wchar_t rootPath[] = {letter, L':', L'\\', L'\0'};
        if (GetDriveTypeW(rootPath) != DRIVE_FIXED) {
            continue;
        }
        if (!IsNtfsOrRefs(rootPath)) {
            continue;
        }

        ffprotocol::VolumeInfo info{};
        info.id = ffprotocol::VolumeId{IdForDriveLetter(letter)};
        info.driveLetter = letter;
        volumes.push_back(info);
    }

    return volumes;
}

wchar_t ResolveVolumeIdToDriveLetter(ffprotocol::VolumeId id) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (const auto& [letter, mappedId] : g_driveLetterToId) {
        if (mappedId == id.value) {
            return letter;
        }
    }
    return L'\0';
}

} // namespace ffindexsvc
