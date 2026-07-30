#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "ffprotocol/Commands.h"
#include "ffprotocol/Dispatch.h"
#include "ffprotocol/Frame.h"
#include "ffprotocol/Records.h"
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

} // namespace

int main() {
    TestOversizedFrameRejectedBeforeAllocation();
    TestUnrecognizedMessageTypeRejected();
    TestUnsupportedStructVersionRejected();
    TestRecordCountPayloadMismatchRejected();
    TestOutOfRangeLengthPrefixedFieldRejectsWholeRecord();
    TestVersionCompatibility();

    if (g_failures > 0) {
        std::fprintf(stderr, "\n%d check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    std::printf("\nall checks passed\n");
    return EXIT_SUCCESS;
}
