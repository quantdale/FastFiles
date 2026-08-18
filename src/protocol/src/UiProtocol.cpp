#include "ffprotocol/UiProtocol.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include "ffprotocol/Dispatch.h"

namespace ffprotocol {

std::optional<UiMessageType> ToUiMessageType(uint16_t raw) noexcept {
    switch (static_cast<UiMessageType>(raw)) {
        case UiMessageType::Subscribe:
        case UiMessageType::SubscribeAck:
        case UiMessageType::RequestDirectory:
        case UiMessageType::DirectoryError:
        case UiMessageType::NewGeneration:
        case UiMessageType::EngineStatus:
        case UiMessageType::ReloadIndexingConfig:
        case UiMessageType::RequestUnavailableVolumes:
        case UiMessageType::UnavailableVolumes:
        case UiMessageType::ForgetUnavailableVolume:
        case UiMessageType::ForgetUnavailableVolumeResult:
        case UiMessageType::RequestFolderAggregate:
        case UiMessageType::FolderAggregateResult:
        case UiMessageType::RequestVolumeStatus:
        case UiMessageType::VolumeStatus:
        case UiMessageType::SetIndexingPaused:
        case UiMessageType::RequestIndexedSearch:
        case UiMessageType::IndexedSearchResult:
        case UiMessageType::CancelIndexedSearch:
        case UiMessageType::FolderAggregateResultV2:
        case UiMessageType::IndexedSearchResultV2:
            return static_cast<UiMessageType>(raw);
        default:
            return std::nullopt;
    }
}

bool IsUiPathLengthValid(uint16_t lengthChars) noexcept {
    return lengthChars > 0 && lengthChars <= kMaxPathLengthChars;
}

bool IsForgetUnavailableVolumeStatusValid(ForgetUnavailableVolumeStatus status) noexcept {
    switch (status) {
        case ForgetUnavailableVolumeStatus::Removed:
        case ForgetUnavailableVolumeStatus::NotFound:
        case ForgetUnavailableVolumeStatus::VolumeAvailable:
        case ForgetUnavailableVolumeStatus::Failed:
            return true;
        default:
            return false;
    }
}

bool IsFolderAggregateStatusValid(FolderAggregateStatus status) noexcept {
    switch (status) {
        case FolderAggregateStatus::Resolved:
        case FolderAggregateStatus::Pending:
        case FolderAggregateStatus::NotFound:
            return true;
        default:
            return false;
    }
}

bool IsIndexedSearchStatusValid(IndexedSearchStatus status) noexcept {
    switch (status) {
        case IndexedSearchStatus::Completed:
        case IndexedSearchStatus::Unavailable:
        case IndexedSearchStatus::InvalidRequest:
        case IndexedSearchStatus::Cancelled:
            return true;
        default:
            return false;
    }
}

bool IsIndexedSearchScopeValid(uint8_t scope) noexcept {
    return scope <= 3;
}

bool IsIndexedSearchSortValid(uint8_t sortField) noexcept {
    // indexed-search-allocated-size D6: every pre-existing sort value (0..5,
    // Relevance..Created) keeps its exact numeric meaning, and the new
    // Size-on-Disk value (6) is validated explicitly rather than interpreted as
    // a continuation of the old range. A switch over the closed set keeps the
    // contract fail-closed: any value outside 0..6 is rejected.
    switch (sortField) {
        case 0: case 1: case 2: case 3: case 4: case 5: case 6: return true;
        default: return false;
    }
}

namespace {

bool AddBytes(size_t left, size_t right, size_t* result) noexcept {
    if (right > (std::numeric_limits<size_t>::max)() - left) {
        return false;
    }
    *result = left + right;
    return true;
}

bool LengthBytes(uint16_t chars, size_t* result) noexcept {
    if (static_cast<size_t>(chars) > (std::numeric_limits<size_t>::max)() / sizeof(wchar_t)) {
        return false;
    }
    *result = static_cast<size_t>(chars) * sizeof(wchar_t);
    return true;
}

bool AppendBytes(std::vector<uint8_t>& destination, const void* data, size_t size,
                 size_t maximum) {
    size_t nextSize = 0;
    if (!AddBytes(destination.size(), size, &nextSize) || nextSize > maximum) {
        return false;
    }
    const auto* bytes = static_cast<const uint8_t*>(data);
    destination.insert(destination.end(), bytes, bytes + size);
    return true;
}

bool IsCandidateLengthValid(uint16_t lengthChars, bool allowEmpty) noexcept {
    return (allowEmpty || lengthChars > 0) && lengthChars <= kMaxPathLengthChars;
}

} // namespace

std::optional<IndexedSearchRequest> ParseIndexedSearchRequest(const uint8_t* payload,
                                                              size_t payloadSize) {
    if (payload == nullptr || payloadSize < sizeof(IndexedSearchRequestHeader) ||
        payloadSize > kMaxFrameSize - sizeof(FrameHeader)) {
        return std::nullopt;
    }

    IndexedSearchRequestHeader header{};
    std::memcpy(&header, payload, sizeof(header));
    if (header.requestId == 0 || header.reserved != 0 || header.descending > 1 ||
        !IsIndexedSearchScopeValid(header.scope) || !IsIndexedSearchSortValid(header.sortField) ||
        !IsUiPathLengthValid(header.queryLengthChars) ||
        header.queryLengthChars > kMaxIndexedSearchQueryLengthChars ||
        header.scopePathLengthChars > kMaxPathLengthChars || header.maxResults == 0 ||
        header.maxResults > kMaxIndexedSearchRequestResults) {
        return std::nullopt;
    }

    size_t queryBytes = 0;
    size_t pathBytes = 0;
    if (!LengthBytes(header.queryLengthChars, &queryBytes) ||
        !LengthBytes(header.scopePathLengthChars, &pathBytes)) {
        return std::nullopt;
    }
    size_t expectedSize = sizeof(header);
    if (!AddBytes(expectedSize, queryBytes, &expectedSize) ||
        !AddBytes(expectedSize, pathBytes, &expectedSize) || expectedSize != payloadSize) {
        return std::nullopt;
    }

    const auto* queryData = reinterpret_cast<const wchar_t*>(payload + sizeof(header));
    const auto* pathData = reinterpret_cast<const wchar_t*>(payload + sizeof(header) + queryBytes);
    IndexedSearchRequest result;
    result.requestId = header.requestId;
    result.scope = header.scope;
    result.sortField = header.sortField;
    result.descending = header.descending != 0;
    result.maxResults = header.maxResults;
    result.query.assign(queryData, header.queryLengthChars);
    result.scopePath.assign(pathData, header.scopePathLengthChars);
    return result;
}

std::optional<IndexedSearchResponse> ParseIndexedSearchResponse(const uint8_t* payload,
                                                                size_t payloadSize) {
    if (payload == nullptr || payloadSize < sizeof(IndexedSearchResultHeader) ||
        payloadSize > kMaxFrameSize - sizeof(FrameHeader)) {
        return std::nullopt;
    }

    IndexedSearchResultHeader header{};
    std::memcpy(&header, payload, sizeof(header));
    if (header.requestId == 0 || !IsIndexedSearchStatusValid(header.status) ||
        header.truncated > 1 || header.reserved != 0 ||
        header.count > kMaxIndexedSearchResultCount) {
        return std::nullopt;
    }

    IndexedSearchResponse result;
    result.requestId = header.requestId;
    result.status = header.status;
    result.truncated = header.truncated != 0;
    result.candidates.reserve(header.count);

    size_t offset = sizeof(header);
    for (uint32_t index = 0; index < header.count; ++index) {
        size_t fixedEnd = 0;
        if (!AddBytes(offset, sizeof(IndexedSearchCandidateHeader), &fixedEnd) ||
            fixedEnd > payloadSize) {
            return std::nullopt;
        }
        IndexedSearchCandidateHeader candidateHeader{};
        std::memcpy(&candidateHeader, payload + offset, sizeof(candidateHeader));
        if (!IsCandidateLengthValid(candidateHeader.nameLengthChars, false) ||
            !IsCandidateLengthValid(candidateHeader.folderLengthChars, true) ||
            (candidateHeader.flags & ~uint8_t{0x03}) != 0 ||
            candidateHeader.reserved[0] != 0 || candidateHeader.reserved[1] != 0 ||
            candidateHeader.reserved[2] != 0) {
            return std::nullopt;
        }
        size_t nameBytes = 0;
        size_t folderBytes = 0;
        if (!LengthBytes(candidateHeader.nameLengthChars, &nameBytes) ||
            !LengthBytes(candidateHeader.folderLengthChars, &folderBytes)) {
            return std::nullopt;
        }
        size_t stringEnd = fixedEnd;
        if (!AddBytes(stringEnd, nameBytes, &stringEnd) ||
            !AddBytes(stringEnd, folderBytes, &stringEnd) || stringEnd > payloadSize) {
            return std::nullopt;
        }

        const auto* nameData = reinterpret_cast<const wchar_t*>(payload + fixedEnd);
        const auto* folderData = reinterpret_cast<const wchar_t*>(payload + fixedEnd + nameBytes);
        IndexedSearchCandidate candidate;
        candidate.name.assign(nameData, candidateHeader.nameLengthChars);
        candidate.folder.assign(folderData, candidateHeader.folderLengthChars);
        candidate.isDirectory = (candidateHeader.flags & 0x01) != 0;
        candidate.persistentIndexed = (candidateHeader.flags & 0x02) != 0;
        candidate.volumeId = candidateHeader.volumeId;
        candidate.id = candidateHeader.id;
        candidate.parentId = candidateHeader.parentId;
        candidate.sizeBytes = candidateHeader.sizeBytes;
        candidate.modifiedTime = candidateHeader.modifiedTime;
        candidate.createdTime = candidateHeader.createdTime;
        result.candidates.push_back(std::move(candidate));
        offset = stringEnd;
    }
    if (offset != payloadSize) {
        return std::nullopt;
    }
    return result;
}

std::vector<uint8_t> SerializeIndexedSearchRequest(const IndexedSearchRequest& request) {
    if (request.requestId == 0 || !IsIndexedSearchScopeValid(request.scope) ||
        !IsIndexedSearchSortValid(request.sortField) || request.query.empty() ||
        request.query.size() > kMaxIndexedSearchQueryLengthChars ||
        request.scopePath.size() > kMaxPathLengthChars || request.maxResults == 0 ||
        request.maxResults > kMaxIndexedSearchRequestResults) {
        return {};
    }
    IndexedSearchRequestHeader header{};
    header.requestId = request.requestId;
    header.scope = request.scope;
    header.sortField = request.sortField;
    header.descending = request.descending ? 1 : 0;
    header.queryLengthChars = static_cast<uint16_t>(request.query.size());
    header.scopePathLengthChars = static_cast<uint16_t>(request.scopePath.size());
    header.maxResults = request.maxResults;

    std::vector<uint8_t> payload(sizeof(header));
    std::memcpy(payload.data(), &header, sizeof(header));
    const size_t queryBytes = request.query.size() * sizeof(wchar_t);
    const size_t pathBytes = request.scopePath.size() * sizeof(wchar_t);
    const auto* queryData = reinterpret_cast<const uint8_t*>(request.query.data());
    payload.insert(payload.end(), queryData, queryData + queryBytes);
    const auto* pathData = reinterpret_cast<const uint8_t*>(request.scopePath.data());
    payload.insert(payload.end(), pathData, pathData + pathBytes);
    return payload;
}

std::vector<uint8_t> SerializeIndexedSearchResponse(const IndexedSearchResponse& response,
                                                    bool* outTruncated) {
    if (outTruncated != nullptr) {
        *outTruncated = false;
    }
    if (response.requestId == 0 || !IsIndexedSearchStatusValid(response.status) ||
        response.candidates.size() > kMaxIndexedSearchResultCount) {
        return {};
    }

    constexpr size_t kMaxPayload = kMaxFrameSize - sizeof(FrameHeader);
    std::vector<uint8_t> payload(sizeof(IndexedSearchResultHeader));
    uint32_t count = 0;
    bool truncated = response.truncated;
    for (const auto& candidate : response.candidates) {
        if (candidate.name.empty() || candidate.name.size() > kMaxPathLengthChars ||
            candidate.folder.size() > kMaxPathLengthChars) {
            return {};
        }
        IndexedSearchCandidateHeader candidateHeader{};
        candidateHeader.nameLengthChars = static_cast<uint16_t>(candidate.name.size());
        candidateHeader.folderLengthChars = static_cast<uint16_t>(candidate.folder.size());
        candidateHeader.flags = static_cast<uint8_t>((candidate.isDirectory ? 0x01 : 0) |
                                                     (candidate.persistentIndexed ? 0x02 : 0));
        candidateHeader.volumeId = candidate.volumeId;
        candidateHeader.id = candidate.id;
        candidateHeader.parentId = candidate.parentId;
        candidateHeader.sizeBytes = candidate.sizeBytes;
        candidateHeader.modifiedTime = candidate.modifiedTime;
        candidateHeader.createdTime = candidate.createdTime;

        size_t nameBytes = candidate.name.size() * sizeof(wchar_t);
        size_t folderBytes = candidate.folder.size() * sizeof(wchar_t);
        size_t candidateBytes = sizeof(candidateHeader);
        if (!AddBytes(candidateBytes, nameBytes, &candidateBytes) ||
            !AddBytes(candidateBytes, folderBytes, &candidateBytes) ||
            candidateBytes > kMaxPayload - payload.size()) {
            truncated = true;
            break;
        }
        const size_t previousSize = payload.size();
        if (!AppendBytes(payload, &candidateHeader, sizeof(candidateHeader), kMaxPayload)) {
            return {};
        }
        const auto* nameData = reinterpret_cast<const uint8_t*>(candidate.name.data());
        const auto* folderData = reinterpret_cast<const uint8_t*>(candidate.folder.data());
        if (!AppendBytes(payload, nameData, nameBytes, kMaxPayload) ||
            !AppendBytes(payload, folderData, folderBytes, kMaxPayload)) {
            payload.resize(previousSize);
            truncated = true;
            break;
        }
        ++count;
    }
    if (count < response.candidates.size()) {
        truncated = true;
    }
    IndexedSearchResultHeader header{};
    header.requestId = response.requestId;
    header.status = response.status;
    header.truncated = truncated ? 1 : 0;
    header.count = count;
    std::memcpy(payload.data(), &header, sizeof(header));
    if (outTruncated != nullptr) {
        *outTruncated = truncated;
    }
    return payload;
}

std::optional<IndexedSearchResponse> ParseIndexedSearchResponseV2(const uint8_t* payload,
                                                                  size_t payloadSize) {
    if (payload == nullptr || payloadSize < sizeof(IndexedSearchResultHeader) ||
        payloadSize > kMaxFrameSize - sizeof(FrameHeader)) {
        return std::nullopt;
    }

    IndexedSearchResultHeader header{};
    std::memcpy(&header, payload, sizeof(header));
    if (header.requestId == 0 || !IsIndexedSearchStatusValid(header.status) ||
        header.truncated > 1 || header.reserved != 0 ||
        header.count > kMaxIndexedSearchResultCount) {
        return std::nullopt;
    }

    IndexedSearchResponse result;
    result.requestId = header.requestId;
    result.status = header.status;
    result.truncated = header.truncated != 0;
    result.candidates.reserve(header.count);

    size_t offset = sizeof(header);
    for (uint32_t index = 0; index < header.count; ++index) {
        size_t fixedEnd = 0;
        if (!AddBytes(offset, sizeof(IndexedSearchCandidateHeaderV2), &fixedEnd) ||
            fixedEnd > payloadSize) {
            return std::nullopt;
        }
        IndexedSearchCandidateHeaderV2 candidateHeader{};
        std::memcpy(&candidateHeader, payload + offset, sizeof(candidateHeader));
        if (!IsCandidateLengthValid(candidateHeader.nameLengthChars, false) ||
            !IsCandidateLengthValid(candidateHeader.folderLengthChars, true) ||
            (candidateHeader.flags & ~uint8_t{0x07}) != 0 ||
            candidateHeader.reserved[0] != 0 || candidateHeader.reserved[1] != 0 ||
            candidateHeader.reserved[2] != 0) {
            return std::nullopt;
        }
        // indexed-search-allocated-size D5: an unknown allocated size must never
        // carry fabricated on-disk bytes. Reject the contradiction rather than
        // decoding a value the engine is not allowed to emit.
        if (!((candidateHeader.flags & 0x04) != 0) && candidateHeader.allocatedSizeBytes != 0) {
            return std::nullopt;
        }
        size_t nameBytes = 0;
        size_t folderBytes = 0;
        if (!LengthBytes(candidateHeader.nameLengthChars, &nameBytes) ||
            !LengthBytes(candidateHeader.folderLengthChars, &folderBytes)) {
            return std::nullopt;
        }
        size_t stringEnd = fixedEnd;
        if (!AddBytes(stringEnd, nameBytes, &stringEnd) ||
            !AddBytes(stringEnd, folderBytes, &stringEnd) || stringEnd > payloadSize) {
            return std::nullopt;
        }

        const auto* nameData = reinterpret_cast<const wchar_t*>(payload + fixedEnd);
        const auto* folderData = reinterpret_cast<const wchar_t*>(payload + fixedEnd + nameBytes);
        IndexedSearchCandidate candidate;
        candidate.name.assign(nameData, candidateHeader.nameLengthChars);
        candidate.folder.assign(folderData, candidateHeader.folderLengthChars);
        candidate.isDirectory = (candidateHeader.flags & 0x01) != 0;
        candidate.persistentIndexed = (candidateHeader.flags & 0x02) != 0;
        candidate.volumeId = candidateHeader.volumeId;
        candidate.id = candidateHeader.id;
        candidate.parentId = candidateHeader.parentId;
        candidate.sizeBytes = candidateHeader.sizeBytes;
        candidate.modifiedTime = candidateHeader.modifiedTime;
        candidate.createdTime = candidateHeader.createdTime;
        candidate.allocatedSizeKnown = (candidateHeader.flags & 0x04) != 0;
        candidate.allocatedSizeBytes = candidateHeader.allocatedSizeBytes;
        result.candidates.push_back(std::move(candidate));
        offset = stringEnd;
    }
    if (offset != payloadSize) {
        return std::nullopt;
    }
    return result;
}

std::vector<uint8_t> SerializeIndexedSearchResponseV2(const IndexedSearchResponse& response,
                                                      bool* outTruncated) {
    if (outTruncated != nullptr) {
        *outTruncated = false;
    }
    if (response.requestId == 0 || !IsIndexedSearchStatusValid(response.status) ||
        response.candidates.size() > kMaxIndexedSearchResultCount) {
        return {};
    }

    constexpr size_t kMaxPayload = kMaxFrameSize - sizeof(FrameHeader);
    std::vector<uint8_t> payload(sizeof(IndexedSearchResultHeader));
    uint32_t count = 0;
    bool truncated = response.truncated;
    for (const auto& candidate : response.candidates) {
        if (candidate.name.empty() || candidate.name.size() > kMaxPathLengthChars ||
            candidate.folder.size() > kMaxPathLengthChars) {
            return {};
        }
        IndexedSearchCandidateHeaderV2 candidateHeader{};
        candidateHeader.nameLengthChars = static_cast<uint16_t>(candidate.name.size());
        candidateHeader.folderLengthChars = static_cast<uint16_t>(candidate.folder.size());
        candidateHeader.flags = static_cast<uint8_t>((candidate.isDirectory ? 0x01 : 0) |
                                                     (candidate.persistentIndexed ? 0x02 : 0) |
                                                     (candidate.allocatedSizeKnown ? 0x04 : 0));
        candidateHeader.volumeId = candidate.volumeId;
        candidateHeader.id = candidate.id;
        candidateHeader.parentId = candidate.parentId;
        candidateHeader.sizeBytes = candidate.sizeBytes;
        candidateHeader.modifiedTime = candidate.modifiedTime;
        candidateHeader.createdTime = candidate.createdTime;
        // indexed-search-allocated-size D5: never emit fabricated on-disk bytes
        // for an unknown candidate -- collapse any stray value to zero.
        candidateHeader.allocatedSizeBytes = candidate.allocatedSizeKnown ? candidate.allocatedSizeBytes : 0;

        size_t nameBytes = candidate.name.size() * sizeof(wchar_t);
        size_t folderBytes = candidate.folder.size() * sizeof(wchar_t);
        size_t candidateBytes = sizeof(candidateHeader);
        if (!AddBytes(candidateBytes, nameBytes, &candidateBytes) ||
            !AddBytes(candidateBytes, folderBytes, &candidateBytes) ||
            candidateBytes > kMaxPayload - payload.size()) {
            truncated = true;
            break;
        }
        const size_t previousSize = payload.size();
        if (!AppendBytes(payload, &candidateHeader, sizeof(candidateHeader), kMaxPayload)) {
            return {};
        }
        const auto* nameData = reinterpret_cast<const uint8_t*>(candidate.name.data());
        const auto* folderData = reinterpret_cast<const uint8_t*>(candidate.folder.data());
        if (!AppendBytes(payload, nameData, nameBytes, kMaxPayload) ||
            !AppendBytes(payload, folderData, folderBytes, kMaxPayload)) {
            payload.resize(previousSize);
            truncated = true;
            break;
        }
        ++count;
    }
    if (count < response.candidates.size()) {
        truncated = true;
    }
    IndexedSearchResultHeader header{};
    header.requestId = response.requestId;
    header.status = response.status;
    header.truncated = truncated ? 1 : 0;
    header.count = count;
    std::memcpy(payload.data(), &header, sizeof(header));
    if (outTruncated != nullptr) {
        *outTruncated = truncated;
    }
    return payload;
}

UiFrameValidationResult ValidateUiFrame(const FrameHeader& header) noexcept {
    if (!IsFrameLengthValid(header.totalLength)) {
        return UiFrameValidationResult::RejectedOversizedFrame;
    }
    if (!ToUiMessageType(header.messageType).has_value()) {
        return UiFrameValidationResult::RejectedUnknownMessageType;
    }
    if (header.structVersion != kCurrentStructVersion) {
        return UiFrameValidationResult::RejectedUnsupportedStructVersion;
    }
    return UiFrameValidationResult::Valid;
}

} // namespace ffprotocol
