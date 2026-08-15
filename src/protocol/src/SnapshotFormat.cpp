#include "ffprotocol/SnapshotFormat.h"

#include <cstring>
#include <limits>
#include <utility>

namespace ffprotocol {

namespace {

bool AddBytes(size_t current, size_t additional, size_t& result) noexcept {
    if (additional > std::numeric_limits<size_t>::max() - current) {
        return false;
    }
    result = current + additional;
    return true;
}

bool AddWideStringBytes(size_t current, const std::wstring& text, size_t& result) noexcept {
    if (text.size() > std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    size_t byteCount = 0;
    if (text.size() > std::numeric_limits<size_t>::max() / sizeof(wchar_t)) {
        return false;
    }
    byteCount = text.size() * sizeof(wchar_t);
    if (!AddBytes(current, sizeof(uint32_t), result)) {
        return false;
    }
    return AddBytes(result, byteCount, result);
}

template <typename DirectoryMap>
size_t SerializedSnapshotSizeImpl(const DirectoryMap& directories) noexcept {
    if (directories.size() > std::numeric_limits<uint32_t>::max()) {
        return 0;
    }

    size_t total = sizeof(uint32_t);
    for (const auto& [path, directory] : directories) {
        if (!AddWideStringBytes(total, path, total)
            || !AddBytes(total, sizeof(uint32_t), total)
            || !AddBytes(total, 5 * sizeof(uint64_t), total)) {
            return 0;
        }
        if (directory.entries.size() > std::numeric_limits<uint32_t>::max()
            || !AddBytes(total, sizeof(uint32_t), total)) {
            return 0;
        }
        for (const auto& entry : directory.entries) {
            // allocated-size-storage-accuracy D6: per-entry fixed size is
            // 1 (isDirectory) + 8 (sizeBytes) + 8 (allocatedSizeBytes) +
            // 1 (allocatedSizeKnown) + 4 (attributes) + 8*7 (timestamps,
            // ids) -- the length-prefixed name follows.
            if (!AddBytes(total, 1 + sizeof(uint32_t) + 2 * sizeof(uint64_t) + 1 + 7 * sizeof(uint64_t), total)
                || !AddWideStringBytes(total, entry.name, total)) {
                return 0;
            }
        }
    }
    return total;
}

class Writer {
public:
    Writer(uint8_t* destination, size_t capacity) : destination_(destination), capacity_(capacity) {}

    bool WriteRaw(const void* source, size_t size) noexcept {
        if (!valid_ || size > capacity_ - offset_) {
            valid_ = false;
            return false;
        }
        if (size != 0) {
            if (destination_ == nullptr || source == nullptr) {
                valid_ = false;
                return false;
            }
            std::memcpy(destination_ + offset_, source, size);
        }
        offset_ += size;
        return true;
    }

    bool WriteU32(uint32_t value) noexcept { return WriteRaw(&value, sizeof(value)); }
    bool WriteU64(uint64_t value) noexcept { return WriteRaw(&value, sizeof(value)); }
    bool WriteByte(uint8_t value) noexcept { return WriteRaw(&value, sizeof(value)); }

    bool WriteWideString(const std::wstring& text) noexcept {
        if (text.size() > std::numeric_limits<uint32_t>::max()
            || text.size() > std::numeric_limits<size_t>::max() / sizeof(wchar_t)) {
            valid_ = false;
            return false;
        }
        const uint32_t length = static_cast<uint32_t>(text.size());
        return WriteU32(length) && WriteRaw(text.data(), text.size() * sizeof(wchar_t));
    }

    bool Valid() const noexcept { return valid_; }
    size_t Offset() const noexcept { return offset_; }

private:
    uint8_t* destination_ = nullptr;
    size_t capacity_ = 0;
    size_t offset_ = 0;
    bool valid_ = true;
};

template <typename DirectoryMap>
bool SerializeSnapshotToImpl(const DirectoryMap& directories, uint8_t* destination,
                             size_t capacity) noexcept {
    const size_t expectedSize = SerializedSnapshotSizeImpl(directories);
    if (expectedSize == 0 || destination == nullptr || capacity < expectedSize) {
        return false;
    }

    Writer writer(destination, capacity);
    if (!writer.WriteU32(static_cast<uint32_t>(directories.size()))) {
        return false;
    }
    for (const auto& [path, directory] : directories) {
        if (!writer.WriteWideString(path)
            || !writer.WriteU32(static_cast<uint32_t>(directory.status))
            || !writer.WriteU64(static_cast<uint64_t>(directory.volumeRowId))
            || !writer.WriteU64(directory.directoryIdLow)
            || !writer.WriteU64(directory.directoryIdHigh)
            || !writer.WriteU64(directory.parentIdLow)
            || !writer.WriteU64(directory.parentIdHigh)
            || !writer.WriteU32(static_cast<uint32_t>(directory.entries.size()))) {
            return false;
        }
        for (const auto& entry : directory.entries) {
            if (!writer.WriteByte(entry.isDirectory ? 1 : 0)
                || !writer.WriteU64(entry.sizeBytes)
                || !writer.WriteU64(entry.allocatedSizeBytes)
                || !writer.WriteByte(entry.allocatedSizeKnown ? 1 : 0)
                || !writer.WriteU32(entry.attributes)
                || !writer.WriteU64(entry.creationTime)
                || !writer.WriteU64(entry.lastModifiedTime)
                || !writer.WriteU64(static_cast<uint64_t>(entry.volumeRowId))
                || !writer.WriteU64(entry.fileIdLow)
                || !writer.WriteU64(entry.fileIdHigh)
                || !writer.WriteU64(entry.parentIdLow)
                || !writer.WriteU64(entry.parentIdHigh)
                || !writer.WriteWideString(entry.name)) {
                return false;
            }
        }
    }
    return writer.Valid() && writer.Offset() == expectedSize;
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

template <typename DirectoryMap>
std::vector<uint8_t> SerializeSnapshotImpl(const DirectoryMap& directories) {
    const size_t size = SerializedSnapshotSizeImpl(directories);
    if (size == 0) {
        return {};
    }
    std::vector<uint8_t> buffer(size);
    if (!SerializeSnapshotToImpl(directories, buffer.data(), buffer.size())) {
        return {};
    }
    return buffer;
}

std::vector<uint8_t> SerializeSnapshot(const std::map<std::wstring, SnapshotDirectory>& directories) {
    return SerializeSnapshotImpl(directories);
}

std::vector<uint8_t> SerializeSnapshot(const CaseInsensitiveSnapshotMap& directories) {
    return SerializeSnapshotImpl(directories);
}

size_t SerializedSnapshotSize(const std::map<std::wstring, SnapshotDirectory>& directories) noexcept {
    return SerializedSnapshotSizeImpl(directories);
}

size_t SerializedSnapshotSize(const CaseInsensitiveSnapshotMap& directories) noexcept {
    return SerializedSnapshotSizeImpl(directories);
}

bool SerializeSnapshotTo(const std::map<std::wstring, SnapshotDirectory>& directories,
                         uint8_t* destination, size_t capacity) noexcept {
    return SerializeSnapshotToImpl(directories, destination, capacity);
}

bool SerializeSnapshotTo(const CaseInsensitiveSnapshotMap& directories,
                         uint8_t* destination, size_t capacity) noexcept {
    return SerializeSnapshotToImpl(directories, destination, capacity);
}

std::optional<std::map<std::wstring, SnapshotDirectory>> ParseSnapshot(const uint8_t* data, size_t size) {
    if (data == nullptr && size != 0) {
        return std::nullopt;
    }
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
        if (statusRaw > static_cast<uint32_t>(DirectoryEnumerationStatus::IoError)) {
            return std::nullopt;
        }

        // Every entry contributes at least 1 (isDirectory) + 4 (name
        // length prefix) bytes -- reject an implausible declared count
        // before reserve() ever sees an attacker-controlled value up to
        // UINT32_MAX (same discipline as Records.h/IsBatchCountPlausible).
        // allocated-size-storage-accuracy D6: +8 (allocatedSizeBytes) + 1
        // (allocatedSizeKnown) per entry.
        constexpr size_t kMinBytesPerEntry = 1 + 2 * sizeof(uint64_t) + 1 + sizeof(uint32_t) + 7 * sizeof(uint64_t);
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
            uint64_t allocatedSizeBytes = 0;
            uint8_t allocatedSizeKnownByte = 0;
            uint32_t attributes = 0;
            uint64_t creationTime = 0, lastModifiedTime = 0, entryVolumeRowId = 0;
            uint64_t fileIdLow = 0, fileIdHigh = 0, entryParentIdLow = 0, entryParentIdHigh = 0;
            std::wstring name;
            if (!reader.ReadByte(isDirectoryByte) || !reader.ReadU64(sizeBytes)
                || !reader.ReadU64(allocatedSizeBytes) || !reader.ReadByte(allocatedSizeKnownByte)
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
            entry.allocatedSizeBytes = allocatedSizeBytes;
            entry.allocatedSizeKnown = allocatedSizeKnownByte != 0;
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
