#include "ffprotocol/SnapshotFormat.h"

#include <cstring>

namespace ffprotocol {

namespace {

void AppendU32(std::vector<uint8_t>& buffer, uint32_t value) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
    buffer.insert(buffer.end(), bytes, bytes + sizeof(value));
}

void AppendU64(std::vector<uint8_t>& buffer, uint64_t value) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
    buffer.insert(buffer.end(), bytes, bytes + sizeof(value));
}

void AppendWString(std::vector<uint8_t>& buffer, const std::wstring& text) {
    AppendU32(buffer, static_cast<uint32_t>(text.size()));
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(text.data());
    buffer.insert(buffer.end(), bytes, bytes + text.size() * sizeof(wchar_t));
}

// A tiny cursor-based reader: every read validates there's enough of the
// buffer left before touching it, and the whole parse is abandoned (never
// a partial result) on any shortfall -- same discipline as
// Records.h/ParseMftBatch.
class Reader {
public:
    Reader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    bool ReadU32(uint32_t& out) {
        if (remaining() < sizeof(uint32_t)) {
            return false;
        }
        std::memcpy(&out, data_ + offset_, sizeof(uint32_t));
        offset_ += sizeof(uint32_t);
        return true;
    }

    bool ReadU64(uint64_t& out) {
        if (remaining() < sizeof(uint64_t)) return false;
        std::memcpy(&out, data_ + offset_, sizeof(uint64_t));
        offset_ += sizeof(uint64_t);
        return true;
    }

    bool ReadByte(uint8_t& out) {
        if (remaining() < 1) {
            return false;
        }
        out = data_[offset_];
        offset_ += 1;
        return true;
    }

    bool ReadWString(std::wstring& out) {
        uint32_t lengthChars = 0;
        if (!ReadU32(lengthChars)) {
            return false;
        }
        const size_t byteLength = static_cast<size_t>(lengthChars) * sizeof(wchar_t);
        if (remaining() < byteLength) {
            return false;
        }
        out.assign(reinterpret_cast<const wchar_t*>(data_ + offset_), lengthChars);
        offset_ += byteLength;
        return true;
    }

    bool ExactlyConsumed() const noexcept { return offset_ == size_; }
    size_t Remaining() const noexcept { return size_ - offset_; }

private:
    size_t remaining() const noexcept { return size_ - offset_; }

    const uint8_t* data_;
    size_t size_;
    size_t offset_ = 0;
};

} // namespace

std::vector<uint8_t> SerializeSnapshot(const std::map<std::wstring, SnapshotDirectory>& directories) {
    std::vector<uint8_t> buffer;
    AppendU32(buffer, static_cast<uint32_t>(directories.size()));

    for (const auto& [path, directory] : directories) {
        AppendWString(buffer, path);
        AppendU32(buffer, static_cast<uint32_t>(directory.status));
        AppendU64(buffer, static_cast<uint64_t>(directory.volumeRowId));
        AppendU64(buffer, directory.directoryIdLow);
        AppendU64(buffer, directory.directoryIdHigh);
        AppendU64(buffer, directory.parentIdLow);
        AppendU64(buffer, directory.parentIdHigh);
        AppendU32(buffer, static_cast<uint32_t>(directory.entries.size()));
        for (const auto& entry : directory.entries) {
            buffer.push_back(entry.isDirectory ? 1 : 0);
            AppendU64(buffer, entry.sizeBytes);
            AppendU32(buffer, entry.attributes);
            AppendU64(buffer, entry.creationTime);
            AppendU64(buffer, entry.lastModifiedTime);
            AppendU64(buffer, static_cast<uint64_t>(entry.volumeRowId));
            AppendU64(buffer, entry.fileIdLow);
            AppendU64(buffer, entry.fileIdHigh);
            AppendU64(buffer, entry.parentIdLow);
            AppendU64(buffer, entry.parentIdHigh);
            AppendWString(buffer, entry.name);
        }
    }
    return buffer;
}

std::optional<std::map<std::wstring, SnapshotDirectory>> ParseSnapshot(const uint8_t* data, size_t size) {
    Reader reader(data, size);

    uint32_t directoryCount = 0;
    if (!reader.ReadU32(directoryCount)) {
        return std::nullopt;
    }

    std::map<std::wstring, SnapshotDirectory> result;
    for (uint32_t i = 0; i < directoryCount; ++i) {
        std::wstring path;
        if (!reader.ReadWString(path)) {
            return std::nullopt;
        }

        uint32_t statusRaw = 0;
        uint32_t entryCount = 0;
        uint64_t volumeRowId = 0, directoryIdLow = 0, directoryIdHigh = 0, parentIdLow = 0, parentIdHigh = 0;
        if (!reader.ReadU32(statusRaw) || !reader.ReadU64(volumeRowId)
            || !reader.ReadU64(directoryIdLow) || !reader.ReadU64(directoryIdHigh)
            || !reader.ReadU64(parentIdLow) || !reader.ReadU64(parentIdHigh)
            || !reader.ReadU32(entryCount)) {
            return std::nullopt;
        }

        // Every entry contributes at least 1 (isDirectory) + 4 (name
        // length prefix) bytes -- reject an implausible declared count
        // before reserve() ever sees an attacker-controlled value up to
        // UINT32_MAX (same discipline as Records.h/IsBatchCountPlausible).
        constexpr size_t kMinBytesPerEntry = 1 + 8 * sizeof(uint64_t) + 2 * sizeof(uint32_t);
        if (static_cast<uint64_t>(entryCount) * kMinBytesPerEntry > reader.Remaining()) {
            return std::nullopt;
        }

        SnapshotDirectory directory;
        directory.status = static_cast<DirectoryEnumerationStatus>(statusRaw);
        directory.volumeRowId = static_cast<int64_t>(volumeRowId);
        directory.directoryIdLow = directoryIdLow;
        directory.directoryIdHigh = directoryIdHigh;
        directory.parentIdLow = parentIdLow;
        directory.parentIdHigh = parentIdHigh;
        directory.entries.reserve(entryCount);

        for (uint32_t e = 0; e < entryCount; ++e) {
            uint8_t isDirectoryByte = 0;
            uint64_t sizeBytes = 0;
            uint32_t attributes = 0;
            uint64_t creationTime = 0, lastModifiedTime = 0, entryVolumeRowId = 0;
            uint64_t fileIdLow = 0, fileIdHigh = 0, entryParentIdLow = 0, entryParentIdHigh = 0;
            std::wstring name;
            if (!reader.ReadByte(isDirectoryByte) || !reader.ReadU64(sizeBytes)
                || !reader.ReadU32(attributes) || !reader.ReadU64(creationTime)
                || !reader.ReadU64(lastModifiedTime) || !reader.ReadU64(entryVolumeRowId)
                || !reader.ReadU64(fileIdLow) || !reader.ReadU64(fileIdHigh)
                || !reader.ReadU64(entryParentIdLow) || !reader.ReadU64(entryParentIdHigh)
                || !reader.ReadWString(name)) {
                return std::nullopt;
            }
            SnapshotDirectoryEntry entry;
            entry.isDirectory = isDirectoryByte != 0;
            entry.sizeBytes = sizeBytes;
            entry.attributes = attributes;
            entry.creationTime = creationTime;
            entry.lastModifiedTime = lastModifiedTime;
            entry.volumeRowId = static_cast<int64_t>(entryVolumeRowId);
            entry.fileIdLow = fileIdLow;
            entry.fileIdHigh = fileIdHigh;
            entry.parentIdLow = entryParentIdLow;
            entry.parentIdHigh = entryParentIdHigh;
            entry.name = std::move(name);
            directory.entries.push_back(std::move(entry));
        }

        result.emplace(std::move(path), std::move(directory));
    }

    if (!reader.ExactlyConsumed()) {
        return std::nullopt; // trailing garbage after the declared content
    }
    return result;
}

} // namespace ffprotocol
