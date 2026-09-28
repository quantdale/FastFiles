#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

#include <windows.h>

#include "ffprotocol/Commands.h"
#include "ffprotocol/Dispatch.h"
#include "ffprotocol/Frame.h"
#include "ffprotocol/IndexHealth.h"
#include "ffprotocol/Settings.h"
#include "ffprotocol/Records.h"
#include "ffprotocol/SnapshotFormat.h"
#include "ffprotocol/UiProtocol.h"
#include "ffprotocol/Version.h"
#include "../TestSupport.h"

using namespace fftest;

namespace {
using namespace ffprotocol;

void TestOversizedFrameRejectedBeforeAllocation() {
    Check(!IsFrameLengthValid(kMaxFrameSize + 1), "oversized totalLength is rejected");
    Check(!IsFrameLengthValid(UINT32_MAX), "near-UINT32_MAX totalLength is rejected, not wrapped");
    Check(IsFrameLengthValid(sizeof(FrameHeader)), "minimum valid totalLength (header only) is accepted");
    Check(!IsFrameLengthValid(sizeof(FrameHeader) - 1), "totalLength smaller than the header is rejected");

    FrameHeader header{};
    header.totalLength = UINT32_MAX;
    header.messageType = static_cast<uint16_t>(MessageType::Heartbeat);
    header.structVersion = kCurrentStructVersion;
    Check(ValidateFrame(header) == FrameValidationResult::RejectedOversizedFrame,
          "ValidateFrame rejects an oversized frame before any payload work");
}

void TestUnrecognizedMessageTypeRejected() {
    Check(!ToMessageType(0).has_value(), "MessageType 0 (no valid command) is rejected");
    Check(!ToMessageType(9999).has_value(), "out-of-range MessageType is rejected");
    Check(ToMessageType(static_cast<uint16_t>(MessageType::Handshake)).has_value(),
          "a real MessageType value round-trips");

    // Bounds around the closed command surface's current end
    // (ScanAttemptStarted = 23): the last assigned value must round-trip, the
    // next unassigned one must not.
    Check(ToMessageType(static_cast<uint16_t>(MessageType::JournalResumeInvalid)).has_value(),
          "JournalResumeInvalid (the journal-gap service->engine message) round-trips");
    Check(ToMessageType(static_cast<uint16_t>(MessageType::JournalRecordBatchV2)).has_value(),
          "JournalRecordBatchV2 (the explicit USN presence-mask format) round-trips");
    Check(ToMessageType(static_cast<uint16_t>(MessageType::ScanRecordBatchV2)).has_value(),
          "ScanRecordBatchV2 (the MFT record layout with allocated size) round-trips");
    Check(ToMessageType(static_cast<uint16_t>(MessageType::JournalRecordBatchV3)).has_value(),
          "JournalRecordBatchV3 (the USN V3 presence-mask format) round-trips");
    Check(ToMessageType(static_cast<uint16_t>(MessageType::ScanFailed)).has_value(),
          "ScanFailed (the terminal scan-failure message) round-trips");
    Check(ToMessageType(static_cast<uint16_t>(MessageType::ScanAttemptStarted)).has_value(),
          "ScanAttemptStarted (the replacement ordering barrier) round-trips");
    Check(ToMessageType(static_cast<uint16_t>(MessageType::ScanAttemptStarted) + 1).has_value() == false,
          "the first unassigned value past ScanAttemptStarted is rejected");
    Check(sizeof(JournalResumeInvalidPayload) == sizeof(VolumeId),
          "JournalResumeInvalidPayload is exactly the fixed VolumeId payload the wire format promises");
    Check(sizeof(ScanFailedPayload) == sizeof(ScanCompletePayload) &&
              sizeof(ScanFailedPayload) == sizeof(VolumeId),
          "ScanFailed carries exactly the fixed VolumeId payload ScanComplete carries");
    Check(sizeof(ScanAttemptStartedPayload) == sizeof(VolumeId),
          "ScanAttemptStarted carries exactly its fixed VolumeId barrier payload");
    const volatile uint16_t failedType = static_cast<uint16_t>(MessageType::ScanFailed);
    const volatile uint16_t completeType = static_cast<uint16_t>(MessageType::ScanComplete);
    Check(failedType != completeType,
          "completion and failure are distinct terminal message types a legacy parser cannot conflate");
    Check(AreStartVolumeScanFlagsValid(0), "a normal-priority scan uses a valid zero flag set");
    Check(AreStartVolumeScanFlagsValid(kStartVolumeScanLowPriority),
          "a reconciliation scan may request low-priority scheduling");
    Check(!AreStartVolumeScanFlagsValid(0x8000), "unknown StartVolumeScan flags are rejected");
    Check(sizeof(StartVolumeScanRequestV1) + sizeof(uint16_t) == sizeof(StartVolumeScanRequest),
          "v2 appends flags after the exact packed v1 scan-request prefix");

    FrameHeader header{};
    header.totalLength = sizeof(FrameHeader);
    header.messageType = 0xBEEF;
    header.structVersion = kCurrentStructVersion;
    Check(ValidateFrame(header) == FrameValidationResult::RejectedUnknownMessageType,
          "ValidateFrame rejects an unrecognized MessageType without crashing");
}

void TestUnsupportedStructVersionRejected() {
    FrameHeader header{};
    header.totalLength = sizeof(FrameHeader);
    header.messageType = static_cast<uint16_t>(MessageType::Heartbeat);
    header.structVersion = kCurrentStructVersion + 1;
    Check(ValidateFrame(header) == FrameValidationResult::RejectedUnsupportedStructVersion,
          "ValidateFrame rejects an unsupported StructVersion");

    header.structVersion = kCurrentStructVersion;
    Check(ValidateFrame(header) == FrameValidationResult::Valid,
          "ValidateFrame accepts a well-formed, known frame");
}

std::vector<uint8_t> EncodeMftRecord(const MftRecordFixedV1& fixed, const std::u16string& name) {
    std::vector<uint8_t> out(sizeof(fixed));
    std::memcpy(out.data(), &fixed, sizeof(fixed));
    const uint8_t* nameBytes = reinterpret_cast<const uint8_t*>(name.data());
    out.insert(out.end(), nameBytes, nameBytes + name.size() * sizeof(char16_t));
    return out;
}

void TestRecordCountPayloadMismatchRejected() {
    MftRecordFixedV1 fixed{};
    fixed.fileReferenceNumber = 1;
    fixed.parentFileReferenceNumber = 2;
    fixed.fileNameLengthChars = 5;
    const std::u16string name = u"hello";
    std::vector<uint8_t> payload = EncodeMftRecord(fixed, name);

    // Declaring 2 records when the payload only contains 1's worth of
    // bytes must be rejected before any record is parsed.
    Check(!IsBatchCountPlausible(2, payload.size(), sizeof(MftRecordFixedV1)),
          "implausible batch count (declared 2, payload holds 1) is rejected pre-parse");
    Check(!ParseMftBatch(payload.data(), payload.size(), 2).has_value(),
          "ParseMftBatch rejects the whole batch on record-count/payload-size mismatch");

    // A single well-formed record parses cleanly.
    auto parsed = ParseMftBatch(payload.data(), payload.size(), 1);
    Check(parsed.has_value() && parsed->size() == 1 && (*parsed)[0].fileName == name,
          "ParseMftBatch parses a well-formed single-record batch");

    // Trailing garbage bytes after the declared record must also be
    // rejected -- the whole payload must be exactly consumed.
    std::vector<uint8_t> withTrailingGarbage = payload;
    withTrailingGarbage.push_back(0xFF);
    Check(!ParseMftBatch(withTrailingGarbage.data(), withTrailingGarbage.size(), 1).has_value(),
          "ParseMftBatch rejects a batch with unconsumed trailing bytes");

    // An implausibly large declared count (well beyond kMaxBatchRecordCount)
    // must never reach a reserve() call.
    Check(!IsBatchCountPlausible(kMaxBatchRecordCount + 1, payload.size(), sizeof(MftRecordFixedV1)),
          "declared count above kMaxBatchRecordCount is rejected before any allocation");
}

void TestOutOfRangeLengthPrefixedFieldRejectsWholeRecord() {
    Check(!IsFileNameLengthValid(0), "zero-length filename is rejected");
    Check(!IsFileNameLengthValid(kMaxFileNameLengthChars + 1), "filename longer than NTFS max is rejected");
    Check(IsFileNameLengthValid(1), "minimum valid filename length is accepted");
    Check(IsFileNameLengthValid(kMaxFileNameLengthChars), "maximum valid filename length is accepted");

    MftRecordFixedV1 fixed{};
    fixed.fileNameLengthChars = kMaxFileNameLengthChars + 1; // out of range, declares more than is present
    std::vector<uint8_t> payload(sizeof(fixed));
    std::memcpy(payload.data(), &fixed, sizeof(fixed));
    // Pad enough trailing bytes that a naive implementation could be
    // tempted to "clamp and continue" rather than reject outright.
    payload.resize(payload.size() + (kMaxFileNameLengthChars + 1) * sizeof(char16_t), 0);

    Check(!ParseMftBatch(payload.data(), payload.size(), 1).has_value(),
          "an out-of-range length-prefixed filename field rejects the whole record, not just that field");

    Check(!ParseMftBatch(nullptr, sizeof(MftRecordFixedV1), 1).has_value(),
          "a null MFT batch buffer with a nonzero size is rejected before dereference");
    Check(!ParseUsnDeltaV2Batch(nullptr, sizeof(UsnDeltaFixedV2), 1).has_value(),
          "a null USN V2 batch buffer with a nonzero size is rejected before dereference");
    Check(!ParseSnapshot(nullptr, sizeof(uint32_t)).has_value(),
          "a null snapshot buffer with a nonzero size is rejected before dereference");
}

void TestUsnV2PresenceMaskRoundTripAndMalformedMask() {
    UsnDeltaV2 record{};
    record.fixed.usn = 101;
    record.fixed.fileReferenceNumber = 0x1234;
    record.fixed.parentFileReferenceNumber = 0x5678;
    record.fixed.reason = 0x00000001;
    record.fixed.timestamp = 111;
    record.fixed.creationTime = 222;
    record.fixed.lastModifiedTime = 333;
    record.fixed.lastAccessTime = 444;
    record.fixed.sizeBytes = 555;
    record.fixed.fileAttributes = 0x20;
    record.fixed.fieldMask = kUsnFieldMaskAll;
    record.fileName = u"new-file.txt";
    record.fixed.fileNameLengthChars = static_cast<uint16_t>(record.fileName.size());
    const auto payload = SerializeUsnDeltaV2Batch({record});
    const auto parsed = ParseUsnDeltaV2Batch(payload.data(), payload.size(), 1);
    Check(parsed.has_value() && parsed->size() == 1 && (*parsed)[0].fixed.creationTime == 222 &&
              (*parsed)[0].fixed.lastAccessTime == 444 && (*parsed)[0].fixed.fieldMask == kUsnFieldMaskAll,
          "USN V2 round-trips authoritative timestamps and explicit field presence");

    auto malformed = payload;
    UsnDeltaFixedV2 malformedFixed{};
    std::memcpy(&malformedFixed, malformed.data(), sizeof(malformedFixed));
    malformedFixed.fieldMask |= 0x80000000u;
    std::memcpy(malformed.data(), &malformedFixed, sizeof(malformedFixed));
    Check(!ParseUsnDeltaV2Batch(malformed.data(), malformed.size(), 1).has_value(),
          "unknown USN V2 presence-mask bits reject the whole batch");

    auto trailing = payload;
    trailing.push_back(0xAA);
    Check(!ParseUsnDeltaV2Batch(trailing.data(), trailing.size(), 1).has_value(),
          "USN V2 trailing bytes reject the whole batch");

    UsnDeltaV1 legacy{};
    legacy.fixed.fileReferenceNumber = 9;
    legacy.fixed.parentFileReferenceNumber = 10;
    legacy.fixed.timestamp = 777;
    legacy.fixed.sizeBytes = 888;
    legacy.fixed.fileAttributes = 0x10;
    legacy.fileName = u"legacy.txt";
    legacy.fixed.fileNameLengthChars = static_cast<uint16_t>(legacy.fileName.size());
    const auto upgraded = UpgradeUsnDeltaV1(legacy);
    Check((upgraded.fixed.fieldMask & (kUsnFieldCreationTime | kUsnFieldLastAccessTime)) == 0 &&
              (upgraded.fixed.fieldMask & kUsnFieldLastModifiedTime) != 0 &&
              upgraded.fixed.lastModifiedTime == legacy.fixed.timestamp,
          "legacy V1 USN compatibility explicitly marks creation/access absent");

    const auto downgraded = DowngradeUsnDeltaV2(record);
    const auto legacyPayload = SerializeUsnDeltaBatch({downgraded});
    const auto legacyParsed = ParseUsnDeltaBatch(legacyPayload.data(), legacyPayload.size(), 1);
    Check(legacyParsed.has_value() && legacyParsed->size() == 1 &&
              (*legacyParsed)[0].fixed.sizeBytes == record.fixed.sizeBytes &&
              (*legacyParsed)[0].fixed.timestamp == record.fixed.timestamp &&
              (*legacyParsed)[0].fileName == record.fileName,
          "a complete V2 USN record downgrades to the legacy V1 wire layout without message 19");
}

void TestMftV2RoundTripAndLegacyLayoutsUnchanged() {
    // allocated-size-storage-accuracy 2.1/2.7: the V1/V2 packed layouts are
    // compatibility contracts -- their sizes must never drift.
    Check(sizeof(MftRecordFixedV1) == 8 * 6 + 4 + 2, "MftRecordFixedV1 size is pinned");
    Check(sizeof(UsnDeltaFixedV1) == 8 * 5 + 4 * 2 + 2, "UsnDeltaFixedV1 size is pinned");
    Check(sizeof(UsnDeltaFixedV2) == 8 * 8 + 4 * 3 + 2, "UsnDeltaFixedV2 size is pinned");
    Check(sizeof(MftRecordFixedV2) == sizeof(MftRecordFixedV1) + sizeof(uint64_t),
          "MftRecordFixedV2 is exactly V1 plus the additive allocated-size field");
    Check(sizeof(UsnDeltaFixedV3) == sizeof(UsnDeltaFixedV2) + sizeof(uint64_t),
          "UsnDeltaFixedV3 is exactly V2 plus the additive allocated-size field");

    MftRecordV2 record{};
    record.fixed.fileReferenceNumber = 0xCAFE;
    record.fixed.parentFileReferenceNumber = 0xBEEF;
    record.fixed.creationTime = 1;
    record.fixed.lastModifiedTime = 2;
    record.fixed.lastAccessTime = 3;
    record.fixed.sizeBytes = 4096;
    record.fixed.allocatedSizeBytes = 8192;
    record.fixed.fileAttributes = 0x20;
    record.fileName = u"allocated.bin";
    record.fixed.fileNameLengthChars = static_cast<uint16_t>(record.fileName.size());

    const auto payload = SerializeMftBatchV2({record});
    const auto parsed = ParseMftBatchV2(payload.data(), payload.size(), 1);
    Check(parsed.has_value() && parsed->size() == 1 && (*parsed)[0].fileName == record.fileName &&
              (*parsed)[0].fixed.allocatedSizeBytes == 8192 && (*parsed)[0].fixed.sizeBytes == 4096,
          "MFT V2 records round-trip allocated and logical size with the filename");

    // An implausible count / trailing garbage / null buffer must reject the
    // whole batch exactly like the V1 parser (same template discipline).
    Check(!ParseMftBatchV2(payload.data(), payload.size(), 2).has_value(),
          "MFT V2 count-vs-bytes mismatch rejects the whole batch");
    auto trailing = payload;
    trailing.push_back(0xEE);
    Check(!ParseMftBatchV2(trailing.data(), trailing.size(), 1).has_value(),
          "MFT V2 trailing bytes reject the whole batch");
    Check(!ParseMftBatchV2(nullptr, sizeof(MftRecordFixedV2), 1).has_value(),
          "a null MFT V2 buffer is rejected before dereference");
}

void TestUsnV3PresenceMaskRoundTripAndDowngradePolicy() {
    // allocated-size-storage-accuracy 2.1/2.5: V3 = V2 + allocated size under
    // an authoritative presence bit; the bit is set only after successful
    // MFT re-enrichment and its absence must survive upgrades.
    UsnDeltaV3 record{};
    record.fixed.usn = 202;
    record.fixed.fileReferenceNumber = 0x1111;
    record.fixed.parentFileReferenceNumber = 0x2222;
    record.fixed.reason = 0x00000001;
    record.fixed.timestamp = 100;
    record.fixed.creationTime = 200;
    record.fixed.lastModifiedTime = 300;
    record.fixed.lastAccessTime = 400;
    record.fixed.sizeBytes = 500;
    record.fixed.allocatedSizeBytes = 600;
    record.fixed.fileAttributes = 0x20;
    record.fixed.fieldMask = kUsnFieldMaskAll;
    record.fileName = u"v3.txt";
    record.fixed.fileNameLengthChars = static_cast<uint16_t>(record.fileName.size());

    const auto payload = SerializeUsnDeltaV3Batch({record});
    const auto parsed = ParseUsnDeltaV3Batch(payload.data(), payload.size(), 1);
    Check(parsed.has_value() && parsed->size() == 1 && (*parsed)[0].fileName == record.fileName &&
              (*parsed)[0].fixed.allocatedSizeBytes == 600 && (*parsed)[0].fixed.sizeBytes == 500 &&
              (*parsed)[0].fixed.fieldMask == kUsnFieldMaskAll,
          "USN V3 round-trips allocated size, logical size, and the full presence mask");

    // Upgrade from V2 preserves the allocated bit as absent -- absence is
    // never synthesized into a value.
    UsnDeltaV2 v2{};
    v2.fixed.usn = 7;
    v2.fixed.fileReferenceNumber = 8;
    v2.fixed.parentFileReferenceNumber = 9;
    v2.fixed.reason = 1;
    v2.fixed.timestamp = 10;
    v2.fixed.lastModifiedTime = 11;
    v2.fixed.fileAttributes = 0x10;
    v2.fixed.fieldMask = kUsnFieldParentId | kUsnFieldName | kUsnFieldSizeBytes |
                         kUsnFieldLastModifiedTime | kUsnFieldAttributes;
    v2.fileName = u"upgraded.txt";
    v2.fixed.fileNameLengthChars = static_cast<uint16_t>(v2.fileName.size());
    const auto upgraded = UpgradeUsnDeltaV2(v2);
    Check((upgraded.fixed.fieldMask & kUsnFieldAllocatedSizeBytes) == 0 &&
              upgraded.fixed.allocatedSizeBytes == 0,
          "upgrading a V2 record keeps the allocated-size bit absent (zero here is absence, not a value)");

    // Downgrade policy: a record carrying the allocated bit is NOT
    // representable for a V2 peer and must be omitted, never zero-filled.
    Check(!IsRepresentableInUsnV2(record), "a V3 record with the allocated-size bit is not representable in V2");
    UsnDeltaV3 withoutAllocated = record;
    withoutAllocated.fixed.fieldMask = record.fixed.fieldMask & ~kUsnFieldAllocatedSizeBytes;
    Check(IsRepresentableInUsnV2(withoutAllocated),
          "a V3 record without the allocated-size bit is representable in V2");
    const auto downgraded = DowngradeUsnDeltaV3(withoutAllocated);
    const auto v2Payload = SerializeUsnDeltaV2Batch({downgraded});
    const auto v2Parsed = ParseUsnDeltaV2Batch(v2Payload.data(), v2Payload.size(), 1);
    Check(v2Parsed.has_value() && v2Parsed->size() == 1 &&
              (*v2Parsed)[0].fixed.sizeBytes == record.fixed.sizeBytes &&
              (*v2Parsed)[0].fixed.fieldMask == withoutAllocated.fixed.fieldMask &&
              (*v2Parsed)[0].fileName == record.fileName,
          "a representable V3 record downgrades to the V2 wire layout losslessly");

    // Unknown mask bits reject the whole V3 batch, same as V2.
    auto malformed = payload;
    UsnDeltaFixedV3 malformedFixed{};
    std::memcpy(&malformedFixed, malformed.data(), sizeof(malformedFixed));
    malformedFixed.fieldMask |= 0x80000000u;
    std::memcpy(malformed.data(), &malformedFixed, sizeof(malformedFixed));
    Check(!ParseUsnDeltaV3Batch(malformed.data(), malformed.size(), 1).has_value(),
          "unknown USN V3 presence-mask bits reject the whole batch");

    // Protocol capability gate: allocated-size records flow only to {2,1}+
    // peers; 2.0 and 1.x peers negotiate down and keep legacy layouts.
    Check(ProtocolSupportsAllocatedSize({2, 1}), "protocol {2,1} supports the allocated-size records");
    Check(ProtocolSupportsAllocatedSize({3, 0}), "a future major with matching minor gates open");
    Check(!ProtocolSupportsAllocatedSize({2, 0}), "protocol {2,0} does not support the allocated-size records");
    Check(!ProtocolSupportsAllocatedSize({1, 99}), "a major-1 peer never receives the allocated-size records");

    // index-durability-false-success-closure 1.3, corrected by
    // legacy-protocol-correctness-and-raw-volume-cancellation-closure 3.2:
    // the ScanFailed terminal is gated the same additive-minor way -- {2,2}+
    // understands it. An older peer has no failure terminal at all: the
    // service writes nothing and ends the connection rather than emitting a
    // false-success ScanComplete (VolumeScanner.cpp SendScanTerminal).
    Check(ProtocolSupportsScanFailure({2, 2}), "protocol {2,2} supports the ScanFailed terminal");
    Check(ProtocolSupportsScanFailure({3, 0}), "a future major with matching minor gates open for ScanFailed");
    Check(!ProtocolSupportsScanFailure({2, 1}), "the previous minor does not support the ScanFailed terminal");
    Check(!ProtocolSupportsScanFailure({2, 0}), "protocol {2,0} does not support the ScanFailed terminal");
    Check(!ProtocolSupportsScanFailure({1, 99}), "a major-1 peer never receives the ScanFailed terminal");
    Check(kCurrentProtocolVersion.major == 2 && kCurrentProtocolVersion.minor >= 2,
          "this build self-declares the ScanFailed capability it implements");

    // scan-generation-integrity-and-residual-certification-closure 3.4:
    // the replacement barrier is separately negotiated. It never resizes
    // either existing terminal payload, so a staged {2,2} peer stays on the
    // safe connection-reset fallback rather than parsing an unknown frame.
    Check(ProtocolSupportsScanAttemptStarted({2, 3}),
          "protocol {2,3} supports the ScanAttemptStarted ordering barrier");
    Check(ProtocolSupportsScanAttemptStarted({3, 0}),
          "a future major gates open for ScanAttemptStarted");
    Check(!ProtocolSupportsScanAttemptStarted({2, 2}),
          "protocol {2,2} does not receive the ScanAttemptStarted barrier");
    Check(!ProtocolSupportsScanAttemptStarted({1, 99}),
          "a major-1 peer never receives the ScanAttemptStarted barrier");
    Check(kCurrentProtocolVersion.major == 2 && kCurrentProtocolVersion.minor >= 3,
          "this build self-declares the ScanAttemptStarted capability it implements");
}

void TestVersionCompatibility() {
    Check(IsVersionCompatible({2, 0}, {2, 5}), "same-major, newer-minor peer is compatible");
    Check(IsVersionCompatible({2, 0}, {1, 9}), "peer exactly one major version behind is compatible");
    Check(!IsVersionCompatible({2, 0}, {0, 9}), "peer more than one major version behind is incompatible");
    Check(!IsVersionCompatible({2, 0}, {3, 0}), "peer with a newer major version is incompatible");
}

// allocated-size-storage-accuracy 6.1: snapshot format v3 round-trips the
// per-entry allocated size + known flag; the version gate fails closed for
// any other format version.
void TestSnapshotV3RoundTripAndVersionGate() {
    // Compile-time pin: snapshot format is v3 (per-entry allocated size).
    static_assert(kSnapshotFormatVersion == 3, "snapshot format must be v3 for allocated-size support");
    // Runtime-derived versions (array indexing defeats /analyze's constant
    // folding) so the fail-closed gate is genuinely exercised: the current
    // version is accepted; any other version is rejected.
    const uint32_t versions[] = {kSnapshotFormatVersion - 1, kSnapshotFormatVersion,
                                 kSnapshotFormatVersion + 1};
    for (uint32_t version : versions) {
        Check(IsSnapshotFormatCompatible(version) == (version == kSnapshotFormatVersion),
              "the snapshot version gate fails closed for every version other than the current one");
    }

    std::map<std::wstring, SnapshotDirectory> directories;
    SnapshotDirectory directory;
    directory.status = DirectoryEnumerationStatus::Success;
    SnapshotDirectoryEntry entry;
    entry.name = L"sparse.vhdx";
    entry.isDirectory = false;
    entry.sizeBytes = 40960;
    entry.allocatedSizeBytes = 8192;
    entry.allocatedSizeKnown = true;
    SnapshotDirectoryEntry unknownEntry;
    unknownEntry.name = L"degraded.bin";
    unknownEntry.sizeBytes = 7;
    unknownEntry.allocatedSizeKnown = false;
    SnapshotDirectoryEntry zeroEntry;
    zeroEntry.name = L"empty.bin";
    zeroEntry.sizeBytes = 0;
    zeroEntry.allocatedSizeBytes = 0;
    zeroEntry.allocatedSizeKnown = true; // genuine zero, distinct from unknown
    directory.entries = {entry, unknownEntry, zeroEntry};
    directories[L"C:\\"] = directory;

    const auto serialized = SerializeSnapshot(directories);
    Check(!serialized.empty(), "v3 snapshot serializes");
    const auto parsed = ParseSnapshot(serialized.data(), serialized.size());
    Check(parsed.has_value(), "v3 snapshot parses");
    if (!parsed) return;
    const auto& parsedDir = parsed->at(L"C:\\");
    Check(parsedDir.entries.size() == 3, "all entries round-trip");
    Check(parsedDir.entries[0].allocatedSizeKnown && parsedDir.entries[0].allocatedSizeBytes == 8192 &&
              parsedDir.entries[0].sizeBytes == 40960,
          "known allocated size round-trips beside logical size");
    Check(!parsedDir.entries[1].allocatedSizeKnown && parsedDir.entries[1].allocatedSizeBytes == 0,
          "unknown stays unknown -- never fabricated as zero");
    Check(parsedDir.entries[2].allocatedSizeKnown && parsedDir.entries[2].allocatedSizeBytes == 0,
          "a genuine zero-byte file round-trips as known zero, distinct from unknown");

    // Malformed v3 payloads follow the reject-the-whole-thing discipline.
    auto truncated = serialized;
    truncated.resize(truncated.size() - 1);
    Check(!ParseSnapshot(truncated.data(), truncated.size()).has_value(),
          "a truncated v3 snapshot is rejected");
    Check(!ParseSnapshot(nullptr, sizeof(uint32_t)).has_value(),
          "a null snapshot buffer is rejected before dereference");
}

void TestUiVolumeLifecycleMessagesAreClosedAndFixedSize() {
    Check(ToUiMessageType(static_cast<uint16_t>(UiMessageType::RequestUnavailableVolumes)).has_value(),
          "RequestUnavailableVolumes is part of the closed UI command surface");
    Check(ToUiMessageType(static_cast<uint16_t>(UiMessageType::ForgetUnavailableVolumeResult)).has_value(),
          "ForgetUnavailableVolumeResult is part of the closed UI command surface");
    Check(ToUiMessageType(static_cast<uint16_t>(UiMessageType::RequestVolumeStatus)).has_value(),
          "RequestVolumeStatus is part of the closed UI command surface");
    Check(ToUiMessageType(static_cast<uint16_t>(UiMessageType::VolumeStatus)).has_value(),
          "VolumeStatus is part of the closed UI command surface");
    Check(ToUiMessageType(static_cast<uint16_t>(UiMessageType::SetIndexingPaused)).has_value(),
          "SetIndexingPaused (settings-and-appearance 9.1 control-plane pause/resume) is part of the closed UI command surface");
    Check(ToUiMessageType(static_cast<uint16_t>(UiMessageType::RequestIndexedSearch)).has_value(),
          "RequestIndexedSearch is part of the closed UI command surface");
    Check(ToUiMessageType(static_cast<uint16_t>(UiMessageType::IndexedSearchResult)).has_value(),
          "IndexedSearchResult is part of the closed UI response surface");
    Check(ToUiMessageType(static_cast<uint16_t>(UiMessageType::CancelIndexedSearch)).has_value(),
          "CancelIndexedSearch is part of the closed UI command surface");
    Check(ToUiMessageType(static_cast<uint16_t>(UiMessageType::FolderAggregateResultV2)).has_value(),
          "FolderAggregateResultV2 (the additive allocated-size aggregate) round-trips");
    Check(ToUiMessageType(static_cast<uint16_t>(UiMessageType::IndexedSearchResultV2)).has_value(),
          "IndexedSearchResultV2 (the additive allocated-size indexed search) round-trips");
    Check(!ToUiMessageType(static_cast<uint16_t>(UiMessageType::IndexedSearchResultV2) + 1).has_value(),
          "the first unassigned UI message after IndexedSearchResultV2 is rejected");
    Check(sizeof(SetIndexingPausedPayload) == sizeof(uint8_t) + sizeof(uint8_t),
          "pause/resume request carries only a scope and a pause flag");
    Check(sizeof(ForgetUnavailableVolumePayload) == sizeof(int64_t),
          "forget request carries only the durable volume row id");
    Check(sizeof(ForgetUnavailableVolumeResultPayload)
              == sizeof(int64_t) + sizeof(ForgetUnavailableVolumeStatus),
          "forget result is a packed row-id/status pair");
    Check(sizeof(UnavailableVolumeRecord) == sizeof(int64_t) + 16 + sizeof(uint32_t) + sizeof(uint64_t),
          "unavailable-volume records have the fixed packed wire shape");
    Check(IsForgetUnavailableVolumeStatusValid(ForgetUnavailableVolumeStatus::Removed),
          "a defined forget result status is accepted");
    Check(!IsForgetUnavailableVolumeStatusValid(static_cast<ForgetUnavailableVolumeStatus>(0xFFFF)),
          "an undefined forget result status is rejected");
    Check(sizeof(VolumeStatusRecord) == sizeof(uint8_t) + sizeof(uint8_t),
          "volume-status records carry only a drive letter and condition flags");
    Check(sizeof(VolumeStatusHeader) == sizeof(uint32_t),
          "volume-status report is a count-prefixed fixed-record list");
}

void TestIndexedSearchWireBoundsAndRoundTrip() {
    IndexedSearchRequest request;
    request.requestId = 41;
    request.scope = 3;
    request.sortField = 2;
    request.descending = true;
    request.maxResults = 10000;
    request.query = L"name:report ext:txt";
    request.scopePath = L"C:\\Users";
    const auto requestPayload = SerializeIndexedSearchRequest(request);
    const auto parsedRequest = ParseIndexedSearchRequest(requestPayload.data(), requestPayload.size());
    Check(parsedRequest.has_value() && parsedRequest->requestId == request.requestId &&
              parsedRequest->query == request.query && parsedRequest->scopePath == request.scopePath &&
              parsedRequest->descending && parsedRequest->maxResults == request.maxResults,
          "indexed-search request round-trips its bounded variable-length fields");

    IndexedSearchResponse response;
    response.requestId = request.requestId;
    response.status = IndexedSearchStatus::Completed;
    response.candidates.push_back({L"report.txt", L"C:\\Users", 42, 20, false, 10, 1, 101, 100, true});
    response.candidates.push_back({L"reports", L"C:\\Users", 0, 30, true, 11, 1, 102, 100, true});
    bool serializerTruncated = false;
    const auto responsePayload = SerializeIndexedSearchResponse(response, &serializerTruncated);
    const auto parsedResponse = ParseIndexedSearchResponse(responsePayload.data(), responsePayload.size());
    Check(parsedResponse.has_value() && !serializerTruncated && parsedResponse->candidates.size() == 2 &&
              parsedResponse->candidates[0].name == L"report.txt" &&
              parsedResponse->candidates[1].isDirectory,
          "indexed-search candidate response round-trips names, folders, and flags");

    auto malformed = responsePayload;
    malformed.push_back(0xAA);
    Check(!ParseIndexedSearchResponse(malformed.data(), malformed.size()),
          "indexed-search response rejects trailing bytes instead of accepting an ambiguous frame");

    IndexedSearchResponse large;
    large.requestId = 7;
    large.status = IndexedSearchStatus::Completed;
    const std::wstring longFolder(32767, L'X');
    for (int index = 0; index != 32; ++index) {
        large.candidates.push_back({L"x.txt", longFolder, 1, 0, false, 0, 1,
                                    static_cast<uint64_t>(index + 1), 5, true});
    }
    const auto boundedPayload = SerializeIndexedSearchResponse(large, &serializerTruncated);
    const auto boundedResponse = ParseIndexedSearchResponse(boundedPayload.data(), boundedPayload.size());
    Check(serializerTruncated && boundedResponse.has_value() && boundedResponse->truncated &&
              boundedResponse->candidates.size() < large.candidates.size(),
          "indexed-search response serialization enforces the frame budget and reports truncation");

    auto invalidRequest = requestPayload;
    auto* requestHeader = reinterpret_cast<IndexedSearchRequestHeader*>(invalidRequest.data());
    requestHeader->maxResults = 0;
    Check(!ParseIndexedSearchRequest(invalidRequest.data(), invalidRequest.size()),
          "indexed-search request rejects zero result limits before allocation");
}

void TestIndexHealthPrecedence() {
    VolumeIndexConditions conditions{true, true, false, false, false};
    Check(DeriveIndexHealth(conditions) == IndexHealth::FullyIndexed, "healthy volume derives Fully Indexed");
    conditions.partiallyIndexed = true;
    Check(DeriveIndexHealth(conditions) == IndexHealth::PartiallyIndexed, "partial scope derives Partially Indexed");
    conditions.needsReconciliation = true;
    Check(DeriveIndexHealth(conditions) == IndexHealth::NeedsReconciliation, "reconciliation outranks partial scope");
    conditions.scanning = true;
    Check(DeriveIndexHealth(conditions) == IndexHealth::CurrentlyIndexing, "active scan outranks reconciliation");
    conditions.privilegedConnectionActive = false;
    Check(DeriveIndexHealth(conditions) == IndexHealth::Unavailable, "unavailable connection has highest precedence");
}
// Task 7.2: the per-volume detail view surfaces every applicable condition,
// not just the headline, so a volume that is both mid-scan and whose
// connection just dropped still shows the in-progress scan condition beneath
// the Unavailable headline (spec "Status Precedence When Multiple Conditions
// Apply" / scenario "Unavailable outranks an in-progress scan"; design D7).
void TestIndexConditionDetailView() {
    // A healthy volume yields exactly one condition: FullyIndexed.
    VolumeIndexConditions healthy{true, true, false, false, false};
    const auto healthyConditions = ApplicableIndexConditions(healthy);
    Check(healthyConditions.size() == 1 && healthyConditions[0] == IndexCondition::FullyIndexed,
          "healthy volume's detail view shows only Fully Indexed");

    // The spec's headline-vs-detail scenario: connection dropped mid-scan.
    // The headline is Unavailable (highest precedence), but the in-progress
    // scan condition must remain visible in the detail view, not lost.
    VolumeIndexConditions droppedMidScan{false, true, true, false, false};
    const auto droppedConditions = ApplicableIndexConditions(droppedMidScan);
    Check(droppedConditions.size() == 2 &&
          droppedConditions[0] == IndexCondition::Unavailable &&
          droppedConditions[1] == IndexCondition::CurrentlyIndexing,
          "unavailable + mid-scan shows both conditions, Unavailable first");
    Check(DeriveIndexHealth(droppedMidScan) == IndexHealth::Unavailable,
          "headline for unavailable + mid-scan is Unavailable");

    // Unreachable volume that is also partially indexed and needs
    // reconciliation: every adverse condition applies, in precedence order.
    VolumeIndexConditions many{false, false, false, true, true};
    const auto manyConditions = ApplicableIndexConditions(many);
    Check(manyConditions.size() == 3 &&
          manyConditions[0] == IndexCondition::Unavailable &&
          manyConditions[1] == IndexCondition::NeedsReconciliation &&
          manyConditions[2] == IndexCondition::PartiallyIndexed,
          "multiple adverse conditions are listed in precedence order, Fully Indexed omitted");

    // A reachable, active, scanning volume that is also partially indexed:
    // Currently Indexing precedes Partially Indexed; Unavailable does not
    // appear (the connection is active and the volume is reachable).
    VolumeIndexConditions scanningPartial{true, true, true, false, true};
    const auto scanningConditions = ApplicableIndexConditions(scanningPartial);
    Check(scanningConditions.size() == 2 &&
          scanningConditions[0] == IndexCondition::CurrentlyIndexing &&
          scanningConditions[1] == IndexCondition::PartiallyIndexed,
          "active scan + partial scope lists both, scan first, no Unavailable");

    Check(std::wstring(IndexConditionName(IndexCondition::CurrentlyIndexing)) == L"Currently Indexing",
          "detail condition has a stable display name");
    Check(std::wstring(IndexHealthName(IndexHealth::Unavailable)) == L"Unavailable",
          "headline status has a stable display name");
}


void TestPrefixRulePrecedence() {
    VolumeSetting volume;
    volume.enabled = true;
    volume.rules = {{L"C:\\Work", true}, {L"C:\\Work\\Private", false}};
    Check(IsPathIncluded(volume, L"C:\\Work\\Readme.txt"), "broader include permits its subtree");
    Check(!IsPathIncluded(volume, L"C:\\Work\\Private\\notes.txt"), "longest matching prefix rule wins");
}

void TestSettingsUtf8RoundTripAndCorruptionRecovery() {
    wchar_t oldLocalAppData[MAX_PATH * 4]{};
    const DWORD oldLength = GetEnvironmentVariableW(L"LOCALAPPDATA", oldLocalAppData, static_cast<DWORD>(std::size(oldLocalAppData)));
    wchar_t tempRoot[MAX_PATH]{};
    GetTempPathW(static_cast<DWORD>(std::size(tempRoot)), tempRoot);
    wchar_t uniquePath[MAX_PATH]{};
    GetTempFileNameW(tempRoot, L"ffs", 0, uniquePath);
    DeleteFileW(uniquePath);
    CreateDirectoryW(uniquePath, nullptr);
    SetEnvironmentVariableW(L"LOCALAPPDATA", uniquePath);

    (void)LoadSettings(true);
    Check(GetFileAttributesW(SettingsPath().c_str()) != INVALID_FILE_ATTRIBUTES,
          "first UI load seeds a complete default settings file");

    Settings settings = DefaultSettings();
    settings.startupLocation = L"C:\\Work\\日本語\\\"quoted\"";
    settings.defaultSearchScope = L"D:\\資料";
    settings.vacuumIntervalHours = 48;
    settings.vacuumSizeGrowthPercent = 15;
    settings.indexing = {{L"C:\\", true, {{L"C:\\Work\\Private", false}}}};
    settings.density = DensityPreference::Comfortable;
    Check(SaveSettings(settings), "settings are written as UTF-8 JSON");
    const Settings loaded = LoadSettings();
    Check(loaded.density == DensityPreference::Comfortable,
          "appearance density round-trips through JSON");
    Check(loaded.startupLocation == settings.startupLocation && loaded.defaultSearchScope == settings.defaultSearchScope,
          "escaped Windows and Unicode paths round-trip without accumulating backslashes");
    Check(loaded.vacuumIntervalHours == settings.vacuumIntervalHours &&
              loaded.vacuumSizeGrowthPercent == settings.vacuumSizeGrowthPercent,
          "VACUUM maintenance settings round-trip through JSON");
    Check(loaded.indexing.size() == 1 && loaded.indexing[0].rules.size() == 1
              && loaded.indexing[0].rules[0].path == settings.indexing[0].rules[0].path,
          "indexing paths round-trip through JSON escaping");
    const std::wstring settingsPathBeforeLegacy = SettingsPath();
    const std::wstring shortcutPathBeforeLegacy = ShortcutSettingsPath();
    Check(settingsPathBeforeLegacy.substr(0, settingsPathBeforeLegacy.find_last_of(L'\\')) ==
              shortcutPathBeforeLegacy.substr(0, shortcutPathBeforeLegacy.find_last_of(L'\\')),
          "settings and shortcut persistence use the same per-user directory");
    {
        std::ofstream legacy(SettingsPath(), std::ios::binary | std::ios::trunc);
        legacy << "{\"shortcuts\":{\"bindings\":["
                  "{\"command\":\"Search\",\"binding\":\"Ctrl+K\"}]}}";
    }
    const LegacyShortcutSettings legacy = LoadLegacyShortcutSettings();
    Check(legacy.sectionPresent && legacy.valid && legacy.bindings.size() == 1 &&
              legacy.bindings.front().first == L"Search" && legacy.bindings.front().second == L"Ctrl+K",
          "legacy settings shortcut section is available only for migration");
    Check(SaveSettings(settings), "canonical general settings save omits legacy shortcut values");
    std::ifstream savedSettings(SettingsPath(), std::ios::binary);
    const std::string savedBytes((std::istreambuf_iterator<char>(savedSettings)), {});
    Check(savedBytes.find("\"shortcuts\"") == std::string::npos,
          "general settings persistence no longer writes a shortcut source of truth");
    savedSettings.close();

    {
        std::ofstream partial(SettingsPath(), std::ios::binary | std::ios::trunc);
        partial << "{\"schemaVersion\":1,\"appearance\":{\"theme\":42},"
                   "\"navigation\":{\"startupLocation\":\"D:\\\\Valid\",\"restorePreviousSession\":false}}";
    }
    const Settings sectionFallback = LoadSettings(false);
    Check(sectionFallback.theme == DefaultSettings().theme && sectionFallback.startupLocation == L"D:\\Valid"
              && !sectionFallback.restorePreviousSession,
          "an invalid section falls back independently without discarding valid sections");
    Check(sectionFallback.density == DensityPreference::Compact,
          "missing appearance density defaults Compact");
    const std::wstring settingsLog = std::wstring(uniquePath) + L"\\FastFiles\\logs\\settings.log";
    Check(GetFileAttributesW(settingsLog.c_str()) != INVALID_FILE_ATTRIBUTES,
          "section fallback writes a metadata-only diagnostic log entry");

    {
        std::ofstream corrupt(SettingsPath(), std::ios::binary | std::ios::trunc);
        corrupt << "{\"theme\": Dark}"; // balanced delimiters, invalid JSON token
    }
    const Settings recovered = LoadSettings(true);
    Check(recovered.theme == DefaultSettings().theme, "syntactically invalid balanced JSON falls back to defaults");
    Check(GetFileAttributesW((SettingsPath() + L".bak").c_str()) != INVALID_FILE_ATTRIBUTES,
          "an unparseable settings file is preserved as settings.json.bak");

    SetEnvironmentVariableW(L"LOCALAPPDATA", oldLength > 0 ? oldLocalAppData : nullptr);
    std::wstring fastFilesPath = std::wstring(uniquePath) + L"\\FastFiles";
    std::wstring backupPath = fastFilesPath + L"\\settings.json.bak";
    std::wstring settingsPath = fastFilesPath + L"\\settings.json";
    std::wstring logPath = fastFilesPath + L"\\logs\\settings.log";
    DeleteFileW(backupPath.c_str());
    DeleteFileW(settingsPath.c_str());
    DeleteFileW(logPath.c_str());
    RemoveDirectoryW((fastFilesPath + L"\\logs").c_str());
    RemoveDirectoryW(fastFilesPath.c_str());
    RemoveDirectoryW(uniquePath);
}

// indexed-search-allocated-size D12: legacy request byte-pinning per sort value.
void TestIndexedSearchLegacyRequestSortBytes() {
    // Every pre-existing sort value (0..5) keeps its exact legacy request byte
    // layout; the new AllocatedSize value (6) is validated separately and must
    // not shift the others. A future change that renumbers the sort field would
    // break the "differ only in the sort byte" pin.
    std::vector<std::vector<uint8_t>> perSort;
    for (uint8_t sort = 0; sort <= 5; ++sort) {
        IndexedSearchRequest request;
        request.requestId = 42;
        request.scope = 3;
        request.sortField = sort;
        request.descending = false;
        request.maxResults = 100;
        request.query = L"report";
        request.scopePath = L"C:\\Root";
        const auto payload = SerializeIndexedSearchRequest(request);
        Check(!payload.empty() && payload.size() == sizeof(IndexedSearchRequestHeader) +
                 request.query.size() * sizeof(wchar_t) + request.scopePath.size() * sizeof(wchar_t),
              "legacy indexed request serializes to its fixed + variable length for every pre-existing sort");
        // sortField byte sits at offset 8 (requestId) + 1 (scope) = 9.
        Check(payload[9] == sort, "legacy indexed request pins the sort field byte for the pre-existing sort value");
        perSort.push_back(payload);
    }
    for (size_t i = 1; i < perSort.size(); ++i) {
        Check(perSort[i].size() == perSort[0].size(), "pre-existing sort requests keep identical legacy length");
        size_t differ = 0;
        for (size_t b = 0; b < perSort[i].size(); ++b) {
            if (perSort[i][b] != perSort[0][b]) ++differ;
        }
        Check(differ == 1, "pre-existing sort requests differ only in the sort field byte");
    }
}

// indexed-search-allocated-size D5/D12: the legacy V1 response is byte-identical
// to pre-change; it decodes allocated size as unknown and never emits fabricated
// on-disk bytes.
void TestIndexedSearchLegacyAllocatedUnknown() {
    IndexedSearchResponse response;
    response.requestId = 7;
    response.status = IndexedSearchStatus::Completed;
    // The candidate sets allocatedSizeBytes/Known, but the legacy V1 path must
    // ignore them and decode as allocated-unknown.
    response.candidates.push_back({L"a.txt", L"C:\\", 42, 0, false, 0, 1, 1, 1, true, 9999, true});
    const auto legacy = SerializeIndexedSearchResponse(response);
    const auto parsed = ParseIndexedSearchResponse(legacy.data(), legacy.size());
    Check(parsed.has_value() && parsed->candidates.size() == 1, "legacy V1 indexed response round-trips");
    Check(!parsed->candidates[0].allocatedSizeKnown && parsed->candidates[0].allocatedSizeBytes == 0,
          "legacy parse maps allocated size to unknown (new fields ignored)");
    // The legacy wire bytes are unchanged by the added fields: a candidate with
    // and without allocated set serialize to byte-identical legacy payloads.
    IndexedSearchResponse unset = response;
    unset.candidates[0].allocatedSizeBytes = 0;
    unset.candidates[0].allocatedSizeKnown = false;
    Check(SerializeIndexedSearchResponse(unset) == legacy,
          "legacy indexed response bytes are unaffected by the added allocated fields");
    Check(sizeof(IndexedSearchCandidateHeader) == 56, "legacy candidate header layout is unchanged (56 bytes)");
}

// indexed-search-allocated-size D6/D12: the additive V2 response round-trips
// known nonzero, known zero, and unknown exactly.
void TestIndexedSearchV2RoundTrips() {
    IndexedSearchResponse response;
    response.requestId = 11;
    response.status = IndexedSearchStatus::Completed;
    response.candidates.push_back({L"known.txt", L"C:\\", 100, 0, false, 0, 1, 1, 1, true, 80, true});
    response.candidates.push_back({L"zero.txt", L"C:\\", 0, 0, false, 0, 1, 2, 1, true, 0, true});
    response.candidates.push_back({L"unknown.txt", L"C:\\", 50, 0, false, 0, 1, 3, 1, true, 0, false});
    const auto payload = SerializeIndexedSearchResponseV2(response);
    Check(!payload.empty(), "V2 indexed response serializes");
    Check(sizeof(IndexedSearchCandidateHeaderV2) == sizeof(IndexedSearchCandidateHeader) + 8,
          "V2 candidate header appends exactly the 8-byte allocated field");
    const auto parsed = ParseIndexedSearchResponseV2(payload.data(), payload.size());
    Check(parsed.has_value() && parsed->candidates.size() == 3, "V2 response round-trips 3 candidates");
    auto find = [&](const wchar_t* name) -> const IndexedSearchCandidate* {
        for (const auto& c : parsed->candidates) {
            if (c.name == name) return &c;
        }
        return nullptr;
    };
    const auto* known = find(L"known.txt");
    const auto* zero = find(L"zero.txt");
    const auto* unknown = find(L"unknown.txt");
    Check(known != nullptr && known->allocatedSizeKnown && known->allocatedSizeBytes == 80,
          "V2 known nonzero allocated size survives the round trip");
    Check(zero != nullptr && zero->allocatedSizeKnown && zero->allocatedSizeBytes == 0,
          "V2 known zero allocated size survives the round trip");
    Check(unknown != nullptr && !unknown->allocatedSizeKnown && unknown->allocatedSizeBytes == 0,
          "V2 unknown allocated size survives the round trip without fabricated bytes");
}

// indexed-search-allocated-size D4/D5/D12: V2 parser fails closed on trailing
// bytes, fabricated unknown bytes, reserved bits, undefined flags, and reports
// frame-budget truncation honestly.
void TestIndexedSearchV2Malformed() {
    IndexedSearchResponse make;
    make.requestId = 1;
    make.status = IndexedSearchStatus::Completed;
    make.candidates.push_back({L"x.txt", L"C:\\", 1, 0, false, 0, 1, 1, 1, true, 5, true});
    auto payload = SerializeIndexedSearchResponseV2(make);
    Check(!payload.empty(), "V2 serializer emits a known candidate");

    auto trailing = payload;
    trailing.push_back(0x00);
    Check(!ParseIndexedSearchResponseV2(trailing.data(), trailing.size()), "V2 rejects trailing bytes");

    // Unknown with fabricated (nonzero) on-disk bytes: clear the known bit while
    // leaving the nonzero allocatedSizeBytes. Flag byte is at header(16)+4.
    auto fabricated = payload;
    fabricated[sizeof(IndexedSearchResultHeader) + 4] &= ~uint8_t{0x04};
    Check(!ParseIndexedSearchResponseV2(fabricated.data(), fabricated.size()),
          "V2 rejects an unknown candidate that carries fabricated on-disk bytes");

    auto reserved = payload;
    reserved[sizeof(IndexedSearchResultHeader) + 5] = 1; // first reserved byte
    Check(!ParseIndexedSearchResponseV2(reserved.data(), reserved.size()), "V2 rejects a nonzero reserved byte");

    auto badFlag = payload;
    badFlag[sizeof(IndexedSearchResultHeader) + 4] |= 0x08; // undefined flag bit
    Check(!ParseIndexedSearchResponseV2(badFlag.data(), badFlag.size()), "V2 rejects an undefined flag bit");

    IndexedSearchResponse large;
    large.requestId = 2;
    large.status = IndexedSearchStatus::Completed;
    const std::wstring longFolder(32767, L'X');
    for (int i = 0; i != 32; ++i) {
        large.candidates.push_back({L"x.txt", longFolder, 1, 0, false, 0, 1,
                                     static_cast<uint64_t>(i + 1), 5, true,
                                     static_cast<uint64_t>(i + 1), true});
    }
    bool truncated = false;
    const auto bound = SerializeIndexedSearchResponseV2(large, &truncated);
    const auto boundParsed = ParseIndexedSearchResponseV2(bound.data(), bound.size());
    Check(truncated && boundParsed.has_value() && boundParsed->truncated &&
              boundParsed->candidates.size() < static_cast<size_t>(large.candidates.size()),
          "V2 serialization enforces the frame budget and reports truncation honestly");
}

} // namespace

int main() {
    TestOversizedFrameRejectedBeforeAllocation();
    TestUnrecognizedMessageTypeRejected();
    TestUnsupportedStructVersionRejected();
    TestUiVolumeLifecycleMessagesAreClosedAndFixedSize();
    TestIndexedSearchWireBoundsAndRoundTrip();
    TestIndexedSearchLegacyRequestSortBytes();
    TestIndexedSearchLegacyAllocatedUnknown();
    TestIndexedSearchV2RoundTrips();
    TestIndexedSearchV2Malformed();
    TestRecordCountPayloadMismatchRejected();
    TestOutOfRangeLengthPrefixedFieldRejectsWholeRecord();
    TestUsnV2PresenceMaskRoundTripAndMalformedMask();
    TestMftV2RoundTripAndLegacyLayoutsUnchanged();
    TestUsnV3PresenceMaskRoundTripAndDowngradePolicy();
    TestSnapshotV3RoundTripAndVersionGate();
    TestVersionCompatibility();
    TestIndexHealthPrecedence();
    TestIndexConditionDetailView();
    TestPrefixRulePrecedence();
    TestSettingsUtf8RoundTripAndCorruptionRecovery();

    if (fftest::FailureCount() > 0) {
        std::fprintf(stderr, "\n%d check(s) failed\n", fftest::FailureCount());
        return EXIT_FAILURE;
    }
    std::printf("\nall checks passed\n");
    return EXIT_SUCCESS;
}
