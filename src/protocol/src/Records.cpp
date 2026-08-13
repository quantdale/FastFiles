#include "ffprotocol/Records.h"
#include <cstring>

namespace ffprotocol {

bool IsFileNameLengthValid(uint16_t lengthChars) noexcept {
    return lengthChars >= 1 && lengthChars <= kMaxFileNameLengthChars;
}

bool IsBatchCountPlausible(uint32_t declaredCount, size_t payloadSize, size_t fixedRecordSize) noexcept {
    if (declaredCount > kMaxBatchRecordCount) {
        return false;
    }
    const uint64_t minPossibleSize =
        static_cast<uint64_t>(declaredCount) * (static_cast<uint64_t>(fixedRecordSize) + sizeof(char16_t));
    return minPossibleSize <= static_cast<uint64_t>(payloadSize);
}

namespace {

template <typename FixedT>
std::optional<std::vector<std::pair<FixedT, std::u16string>>> ParseBatch(
    const uint8_t* payload, size_t payloadSize, uint32_t declaredCount) {
    if (payload == nullptr && payloadSize != 0) {
        return std::nullopt;
    }
    if (!IsBatchCountPlausible(declaredCount, payloadSize, sizeof(FixedT))) {
        return std::nullopt;
    }

    std::vector<std::pair<FixedT, std::u16string>> records;
    records.reserve(declaredCount);

    size_t offset = 0;
    for (uint32_t i = 0; i < declaredCount; ++i) {
        if (offset > payloadSize || sizeof(FixedT) > payloadSize - offset) {
            return std::nullopt;
        }
        FixedT fixed;
        std::memcpy(&fixed, payload + offset, sizeof(FixedT));
        offset += sizeof(FixedT);

        if (!IsFileNameLengthValid(fixed.fileNameLengthChars)) {
            return std::nullopt;
        }
        const size_t fileNameBytes = static_cast<size_t>(fixed.fileNameLengthChars) * sizeof(char16_t);
        if (fileNameBytes > payloadSize - offset) {
            return std::nullopt;
        }
        std::u16string fileName(fixed.fileNameLengthChars, u'\0');
        std::memcpy(fileName.data(), payload + offset, fileNameBytes);
        offset += fileNameBytes;

        records.emplace_back(fixed, std::move(fileName));
    }

    // Every declared record must exactly consume the payload -- trailing
    // bytes indicate a mismatch between declared count and actual data.
    if (offset != payloadSize) {
        return std::nullopt;
    }

    return records;
}

} // namespace

std::optional<std::vector<MftRecordV1>> ParseMftBatch(
    const uint8_t* payload, size_t payloadSize, uint32_t declaredCount) {
    auto parsed = ParseBatch<MftRecordFixedV1>(payload, payloadSize, declaredCount);
    if (!parsed) {
        return std::nullopt;
    }
    std::vector<MftRecordV1> result;
    result.reserve(parsed->size());
    for (auto& [fixed, fileName] : *parsed) {
        result.push_back(MftRecordV1{fixed, std::move(fileName)});
    }
    return result;
}

std::optional<std::vector<UsnDeltaV1>> ParseUsnDeltaBatch(
    const uint8_t* payload, size_t payloadSize, uint32_t declaredCount) {
    auto parsed = ParseBatch<UsnDeltaFixedV1>(payload, payloadSize, declaredCount);
    if (!parsed) {
        return std::nullopt;
    }
    std::vector<UsnDeltaV1> result;
    result.reserve(parsed->size());
    for (auto& [fixed, fileName] : *parsed) {
        result.push_back(UsnDeltaV1{fixed, std::move(fileName)});
    }
    return result;
}

std::optional<std::vector<UsnDeltaV2>> ParseUsnDeltaV2Batch(
    const uint8_t* payload, size_t payloadSize, uint32_t declaredCount) {
    auto parsed = ParseBatch<UsnDeltaFixedV2>(payload, payloadSize, declaredCount);
    if (!parsed) return std::nullopt;
    std::vector<UsnDeltaV2> result;
    result.reserve(parsed->size());
    for (auto& [fixed, fileName] : *parsed) {
        if ((fixed.fieldMask & ~kUsnFieldMaskAll) != 0) return std::nullopt;
        result.push_back(UsnDeltaV2{fixed, std::move(fileName)});
    }
    return result;
}

UsnDeltaV2 UpgradeUsnDeltaV1(const UsnDeltaV1& record) {
    UsnDeltaV2 result{};
    result.fixed.usn = record.fixed.usn;
    result.fixed.fileReferenceNumber = record.fixed.fileReferenceNumber;
    result.fixed.parentFileReferenceNumber = record.fixed.parentFileReferenceNumber;
    result.fixed.reason = record.fixed.reason;
    result.fixed.timestamp = record.fixed.timestamp;
    result.fixed.lastModifiedTime = record.fixed.timestamp;
    result.fixed.sizeBytes = record.fixed.sizeBytes;
    result.fixed.fileAttributes = record.fixed.fileAttributes;
    result.fixed.fieldMask = kUsnFieldParentId | kUsnFieldName | kUsnFieldSizeBytes |
                             kUsnFieldLastModifiedTime | kUsnFieldAttributes;
    result.fixed.fileNameLengthChars = record.fixed.fileNameLengthChars;
    result.fileName = record.fileName;
    return result;
}

UsnDeltaV1 DowngradeUsnDeltaV2(const UsnDeltaV2& record) {
    UsnDeltaV1 result{};
    result.fixed.usn = record.fixed.usn;
    result.fixed.fileReferenceNumber = record.fixed.fileReferenceNumber;
    result.fixed.parentFileReferenceNumber = record.fixed.parentFileReferenceNumber;
    result.fixed.reason = record.fixed.reason;
    result.fixed.timestamp = record.fixed.timestamp;
    result.fixed.sizeBytes = record.fixed.sizeBytes;
    result.fixed.fileAttributes = record.fixed.fileAttributes;
    result.fixed.fileNameLengthChars = record.fixed.fileNameLengthChars;
    result.fileName = record.fileName;
    return result;
}

namespace {

template <typename RecordT>
std::vector<uint8_t> SerializeBatch(const std::vector<RecordT>& records) {
    std::vector<uint8_t> out;
    size_t total = 0;
    for (const auto& record : records) {
        if (!IsFileNameLengthValid(record.fixed.fileNameLengthChars)
            || record.fixed.fileNameLengthChars != record.fileName.size()) {
            continue;
        }
        total += sizeof(record.fixed) + record.fileName.size() * sizeof(char16_t);
    }
    out.reserve(total);

    for (const auto& record : records) {
        if (!IsFileNameLengthValid(record.fixed.fileNameLengthChars)
            || record.fixed.fileNameLengthChars != record.fileName.size()) {
            continue;
        }
        const auto* fixedBytes = reinterpret_cast<const uint8_t*>(&record.fixed);
        out.insert(out.end(), fixedBytes, fixedBytes + sizeof(record.fixed));
        const auto* nameBytes = reinterpret_cast<const uint8_t*>(record.fileName.data());
        out.insert(out.end(), nameBytes, nameBytes + record.fileName.size() * sizeof(char16_t));
    }
    return out;
}

} // namespace

std::vector<uint8_t> SerializeMftBatch(const std::vector<MftRecordV1>& records) {
    return SerializeBatch(records);
}

std::vector<uint8_t> SerializeUsnDeltaBatch(const std::vector<UsnDeltaV1>& records) {
    return SerializeBatch(records);
}

std::vector<uint8_t> SerializeUsnDeltaV2Batch(const std::vector<UsnDeltaV2>& records) {
    return SerializeBatch(records);
}

} // namespace ffprotocol
