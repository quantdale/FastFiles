// Task 7.7: fuzz the frame parser (oversized frames, record-count
// mismatches, out-of-range length-prefixed fields).
//
// Scope note: this exercises ffprotocol directly -- the shared library
// both FastFilesIndexSvc and FastFilesEngine link against and parse every
// inbound frame/record through (via ffipc::ReadFrame -> ValidateFrame /
// ValidateUiFrame, and ParseMftBatch/ParseUsnDeltaBatch once wired up by
// the follow-up MFT/USN change). Actually throwing malformed bytes at a
// live named pipe end-to-end additionally requires a running
// FastFilesIndexSvc/FastFilesEngine process on real Windows, which this
// sandboxed environment cannot do (see task 1.4 session notes) -- that
// end-to-end run is tracked as a follow-up manual/CI verification step,
// not skipped silently.
//
// Uses a fixed seed for reproducibility: a failure here should be
// re-runnable byte-for-byte, not a one-off flake.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

#include "ffprotocol/Commands.h"
#include "ffprotocol/Dispatch.h"
#include "ffprotocol/Frame.h"
#include "ffprotocol/Records.h"
#include "ffprotocol/SnapshotFormat.h"
#include "ffprotocol/UiProtocol.h"
#include "../TestSupport.h"

using namespace fftest;

namespace {

constexpr uint32_t kSeed = 0xF457F17E;
constexpr int kIterationsPerFuzzer = 20000;

std::vector<uint8_t> RandomBytes(std::mt19937& rng, size_t size) {
    std::uniform_int_distribution<int> byteDist(0, 255);
    std::vector<uint8_t> bytes(size);
    for (auto& b : bytes) {
        b = static_cast<uint8_t>(byteDist(rng));
    }
    return bytes;
}

// Every totalLength this generates is plausible-adjacent (near the header
// size, near kMaxFrameSize, or fully random up to UINT32_MAX) -- the
// values most likely to expose an off-by-one or an integer-overflow bug,
// rather than uniformly random noise that's almost always trivially
// oversized.
uint32_t InterestingTotalLength(std::mt19937& rng) {
    std::uniform_int_distribution<int> which(0, 4);
    std::uniform_int_distribution<uint32_t> fullRange(0, UINT32_MAX);
    switch (which(rng)) {
        case 0: return static_cast<uint32_t>(sizeof(ffprotocol::FrameHeader)) - 1;
        case 1: return static_cast<uint32_t>(sizeof(ffprotocol::FrameHeader));
        case 2: return ffprotocol::kMaxFrameSize;
        case 3: return ffprotocol::kMaxFrameSize + 1;
        default: return fullRange(rng);
    }
}

void FuzzServiceFrameValidation(std::mt19937& rng) {
    std::uniform_int_distribution<uint16_t> messageTypeDist(0, 0xFFFF);
    std::uniform_int_distribution<uint16_t> structVersionDist(0, 8);

    for (int i = 0; i < kIterationsPerFuzzer; ++i) {
        ffprotocol::FrameHeader header{};
        header.totalLength = InterestingTotalLength(rng);
        header.messageType = messageTypeDist(rng);
        header.structVersion = structVersionDist(rng);

        // Never allocates a buffer sized from totalLength before this
        // returns -- the fuzz property is "doesn't crash, and rejects
        // anything outside the exact valid combination".
        const auto result = ffprotocol::ValidateFrame(header);

        const bool lengthOk = ffprotocol::IsFrameLengthValid(header.totalLength);
        const bool typeOk = ffprotocol::ToMessageType(header.messageType).has_value();
        const bool versionOk = header.structVersion == ffprotocol::kCurrentStructVersion;

        if (!lengthOk) {
            Check(result == ffprotocol::FrameValidationResult::RejectedOversizedFrame,
                  "oversized/undersized totalLength is always rejected as such");
        } else if (!typeOk) {
            Check(result == ffprotocol::FrameValidationResult::RejectedUnknownMessageType,
                  "unknown messageType is rejected once length is valid");
        } else if (!versionOk) {
            Check(result == ffprotocol::FrameValidationResult::RejectedUnsupportedStructVersion,
                  "unsupported structVersion is rejected once length/type are valid");
        } else {
            Check(result == ffprotocol::FrameValidationResult::Valid, "a fully valid header is accepted");
        }
    }
}

void FuzzUiFrameValidation(std::mt19937& rng) {
    std::uniform_int_distribution<uint16_t> messageTypeDist(0, 0xFFFF);

    for (int i = 0; i < kIterationsPerFuzzer; ++i) {
        ffprotocol::FrameHeader header{};
        header.totalLength = InterestingTotalLength(rng);
        header.messageType = messageTypeDist(rng);
        header.structVersion = ffprotocol::kCurrentStructVersion;

        const auto result = ffprotocol::ValidateUiFrame(header);
        const bool lengthOk = ffprotocol::IsFrameLengthValid(header.totalLength);
        const bool typeOk = ffprotocol::ToUiMessageType(header.messageType).has_value();

        if (!lengthOk) {
            Check(result == ffprotocol::UiFrameValidationResult::RejectedOversizedFrame,
                  "UI protocol: oversized/undersized totalLength is always rejected");
        } else if (!typeOk) {
            Check(result == ffprotocol::UiFrameValidationResult::RejectedUnknownMessageType,
                  "UI protocol: unknown messageType is rejected once length is valid");
        } else {
            Check(result == ffprotocol::UiFrameValidationResult::Valid, "UI protocol: a fully valid header is accepted");
        }
    }
}

// Declared record counts deliberately span from 0 to well past
// kMaxBatchRecordCount, against payloads of random (often mismatched)
// size -- this is the record-count/payload-size mismatch category from
// task 7.7, generalized from test_protocol.cpp's fixed cases.
void FuzzMftBatchParsing(std::mt19937& rng) {
    std::uniform_int_distribution<size_t> payloadSizeDist(0, 4096);
    std::uniform_int_distribution<uint32_t> declaredCountDist(0, ffprotocol::kMaxBatchRecordCount * 2);

    for (int i = 0; i < kIterationsPerFuzzer; ++i) {
        const std::vector<uint8_t> payload = RandomBytes(rng, payloadSizeDist(rng));
        const uint32_t declaredCount = declaredCountDist(rng);

        const bool plausible = ffprotocol::IsBatchCountPlausible(declaredCount, payload.size(), sizeof(ffprotocol::MftRecordFixedV1));
        const auto parsed = ffprotocol::ParseMftBatch(payload.data(), payload.size(), declaredCount);

        if (!plausible) {
            Check(!parsed.has_value(), "an implausible declared record count never produces a parsed batch");
        }
        if (parsed.has_value()) {
            Check(parsed->size() == declaredCount, "a successfully parsed batch has exactly the declared record count");
            for (const auto& record : *parsed) {
                Check(ffprotocol::IsFileNameLengthValid(static_cast<uint16_t>(record.fileName.size())),
                      "every parsed record's filename length is within the valid range");
            }
        }
    }
}

void FuzzUsnDeltaBatchParsing(std::mt19937& rng) {
    std::uniform_int_distribution<size_t> payloadSizeDist(0, 4096);
    std::uniform_int_distribution<uint32_t> declaredCountDist(0, ffprotocol::kMaxBatchRecordCount * 2);

    for (int i = 0; i < kIterationsPerFuzzer; ++i) {
        const std::vector<uint8_t> payload = RandomBytes(rng, payloadSizeDist(rng));
        const uint32_t declaredCount = declaredCountDist(rng);

        const auto parsed = ffprotocol::ParseUsnDeltaBatch(payload.data(), payload.size(), declaredCount);
        if (parsed.has_value()) {
            Check(parsed->size() == declaredCount, "a successfully parsed USN delta batch has exactly the declared count");
        }
    }
}

// The shared-memory snapshot format (engine -> UI) uses the same
// reject-the-whole-thing-on-malformed-input discipline; fuzzed here for
// the same reason as the wire formats above.
void FuzzSnapshotParsing(std::mt19937& rng) {
    std::uniform_int_distribution<size_t> sizeDist(0, 2048);
    for (int i = 0; i < kIterationsPerFuzzer; ++i) {
        const std::vector<uint8_t> data = RandomBytes(rng, sizeDist(rng));
        // The only property under fuzzing is "never crashes, whatever it
        // returns is internally consistent" -- ParseSnapshot's own
        // exactly-consumed check is exercised implicitly.
        volatile auto result = ffprotocol::ParseSnapshot(data.data(), data.size());
        (void)result;
    }
}

} // namespace

int main() {
    std::mt19937 rng(kSeed);

    FuzzServiceFrameValidation(rng);
    FuzzUiFrameValidation(rng);
    FuzzMftBatchParsing(rng);
    FuzzUsnDeltaBatchParsing(rng);
    FuzzSnapshotParsing(rng);

    if (fftest::FailureCount() > 0) {
        std::fprintf(stderr, "\n%d fuzz check(s) failed\n", fftest::FailureCount());
        return EXIT_FAILURE;
    }
    std::printf("all fuzz checks passed (%d iterations/fuzzer, seed 0x%08X)\n", kIterationsPerFuzzer, kSeed);
    return EXIT_SUCCESS;
}
