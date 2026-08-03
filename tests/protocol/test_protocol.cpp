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
#include "ffprotocol/UiProtocol.h"
#include "ffprotocol/Version.h"

namespace {

int g_failures = 0;

void Check(bool condition, const char* description) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++g_failures;
    } else {
        std::printf("ok: %s\n", description);
    }
}

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
    // (index-storage-and-scanning's JournalResumeInvalid = 18): the last
    // assigned value must round-trip, the next unassigned one must not.
    Check(ToMessageType(static_cast<uint16_t>(MessageType::JournalResumeInvalid)).has_value(),
          "JournalResumeInvalid (the newest service->engine message) round-trips");
    Check(ToMessageType(static_cast<uint16_t>(MessageType::JournalResumeInvalid) + 1).has_value() == false,
          "the first unassigned value past JournalResumeInvalid is rejected");
    Check(sizeof(JournalResumeInvalidPayload) == sizeof(VolumeId),
          "JournalResumeInvalidPayload is exactly the fixed VolumeId payload the wire format promises");
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
}

void TestVersionCompatibility() {
    Check(IsVersionCompatible({2, 0}, {2, 5}), "same-major, newer-minor peer is compatible");
    Check(IsVersionCompatible({2, 0}, {1, 9}), "peer exactly one major version behind is compatible");
    Check(!IsVersionCompatible({2, 0}, {0, 9}), "peer more than one major version behind is incompatible");
    Check(!IsVersionCompatible({2, 0}, {3, 0}), "peer with a newer major version is incompatible");
}

void TestUiVolumeLifecycleMessagesAreClosedAndFixedSize() {
    Check(ToUiMessageType(static_cast<uint16_t>(UiMessageType::RequestUnavailableVolumes)).has_value(),
          "RequestUnavailableVolumes is part of the closed UI command surface");
    Check(ToUiMessageType(static_cast<uint16_t>(UiMessageType::ForgetUnavailableVolumeResult)).has_value(),
          "ForgetUnavailableVolumeResult is part of the closed UI command surface");
    Check(!ToUiMessageType(static_cast<uint16_t>(UiMessageType::FolderAggregateResult) + 1).has_value(),
          "the first unassigned UI message after volume lifecycle commands is rejected");
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
    settings.indexing = {{L"C:\\", true, {{L"C:\\Work\\Private", false}}}};
    Check(SaveSettings(settings), "settings are written as UTF-8 JSON");
    const Settings loaded = LoadSettings();
    Check(loaded.startupLocation == settings.startupLocation && loaded.defaultSearchScope == settings.defaultSearchScope,
          "escaped Windows and Unicode paths round-trip without accumulating backslashes");
    Check(loaded.indexing.size() == 1 && loaded.indexing[0].rules.size() == 1
              && loaded.indexing[0].rules[0].path == settings.indexing[0].rules[0].path,
          "indexing paths round-trip through JSON escaping");

    {
        std::ofstream partial(SettingsPath(), std::ios::binary | std::ios::trunc);
        partial << "{\"schemaVersion\":1,\"appearance\":{\"theme\":42},"
                   "\"navigation\":{\"startupLocation\":\"D:\\\\Valid\",\"restorePreviousSession\":false}}";
    }
    const Settings sectionFallback = LoadSettings(false);
    Check(sectionFallback.theme == DefaultSettings().theme && sectionFallback.startupLocation == L"D:\\Valid"
              && !sectionFallback.restorePreviousSession,
          "an invalid section falls back independently without discarding valid sections");
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

} // namespace

int main() {
    TestOversizedFrameRejectedBeforeAllocation();
    TestUnrecognizedMessageTypeRejected();
    TestUnsupportedStructVersionRejected();
    TestUiVolumeLifecycleMessagesAreClosedAndFixedSize();
    TestRecordCountPayloadMismatchRejected();
    TestOutOfRangeLengthPrefixedFieldRejectsWholeRecord();
    TestVersionCompatibility();
    TestIndexHealthPrecedence();
    TestIndexConditionDetailView();
    TestPrefixRulePrecedence();
    TestSettingsUtf8RoundTripAndCorruptionRecovery();

    if (g_failures > 0) {
        std::fprintf(stderr, "\n%d check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    std::printf("\nall checks passed\n");
    return EXIT_SUCCESS;
}
