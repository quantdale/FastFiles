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
    if (!IsBatchCountPlausible(declaredCount, payloadSize, sizeof(FixedT))) {
        return std::nullopt;
    }

    std::vector<std::pair<FixedT, std::u16string>> records;
    records.reserve(declaredCount);

    size_t offset = 0;
    for (uint32_t i = 0; i < declaredCount; ++i) {
        if (offset + sizeof(FixedT) > payloadSize) {
            return std::nullopt;
        }
        FixedT fixed;
        std::memcpy(&fixed, payload + offset, sizeof(FixedT));
        offset += sizeof(FixedT);

        if (!IsFileNameLengthValid(fixed.fileNameLengthChars)) {
            return std::nullopt;
        }
        const size_t fileNameBytes = static_cast<size_t>(fixed.fileNameLengthChars) * sizeof(char16_t);
        if (offset + fileNameBytes > payloadSize) {
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

namespace {

template <typename FixedT, typename RecordT>
std::vector<uint8_t> SerializeBatch(const std::vector<RecordT>& records) {
    size_t totalSize = 0;
    for (const auto& record : records) {
        totalSize += sizeof(FixedT) + record.fileName.size() * sizeof(char16_t);
    }

    std::vector<uint8_t> buffer;
    buffer.reserve(totalSize);
    for (const auto& record : records) {
        const auto* fixedBytes = reinterpret_cast<const uint8_t*>(&record.fixed);
        buffer.insert(buffer.end(), fixedBytes, fixedBytes + sizeof(FixedT));
        const auto* nameBytes = reinterpret_cast<const uint8_t*>(record.fileName.data());
        buffer.insert(buffer.end(), nameBytes, nameBytes + record.fileName.size() * sizeof(char16_t));
    }
    return buffer;
}

} // namespace

std::vector<uint8_t> SerializeMftBatch(const std::vector<MftRecordV1>& records) {
    return SerializeBatch<MftRecordFixedV1>(records);
}

std::vector<uint8_t> SerializeUsnDeltaBatch(const std::vector<UsnDeltaV1>& records) {
    return SerializeBatch<UsnDeltaFixedV1>(records);
}

} // namespace ffprotocol
