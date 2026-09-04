// ntfs-observe contract tests (FC-003/FC-004 regression): pure-helper
// coverage for the bounded read-only MFT/USN certification probe. Uses
// synthetic byte fixtures only -- this suite never touches a live volume
// and must not be mistaken for production NTFS certification.
#include <windows.h>

#include <winioctl.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "../../tests/TestSupport.h"
#include "NtfsObserve.h"

namespace {

using ffntfsobserve::ClassifyObserveStatus;
using ffntfsobserve::FormatHexU64;
using ffntfsobserve::JoinParentPath;
using ffntfsobserve::ObserveStatus;
using ffntfsobserve::ObserveStatusName;
using ffntfsobserve::ParseHexU64;
using ffntfsobserve::WalkEnumBuffer;

void TestClassification() {
    fftest::Check(ClassifyObserveStatus(true, ERROR_SUCCESS, true, true, 1, true) ==
                      ObserveStatus::Observed,
                  "full semantic observation classifies OBSERVED");
    fftest::Check(ClassifyObserveStatus(false, ERROR_ACCESS_DENIED, true, false, 0, false) ==
                      ObserveStatus::Blocked,
                  "denied volume open classifies BLOCKED, never PASS");
    fftest::Check(ClassifyObserveStatus(false, ERROR_SUCCESS, false, false, 0, false) ==
                      ObserveStatus::Blocked,
                  "unopened volume classifies BLOCKED even without error");
    fftest::Check(ClassifyObserveStatus(true, ERROR_SUCCESS, false, true, 3, true) ==
                      ObserveStatus::Blocked,
                  "non-NTFS volume classifies BLOCKED");
    fftest::Check(ClassifyObserveStatus(true, ERROR_SUCCESS, true, false, 0, true) ==
                      ObserveStatus::Failed,
                  "failed enumeration on an opened volume is FAIL");
    fftest::Check(ClassifyObserveStatus(true, ERROR_SUCCESS, true, true, 0, true) ==
                      ObserveStatus::Failed,
                  "zero parsed records is FAIL, not silent PASS");
    fftest::Check(ClassifyObserveStatus(true, ERROR_SUCCESS, true, true, 2, false) ==
                      ObserveStatus::Failed,
                  "unqueried journal is FAIL");
    fftest::Check(std::strcmp(ObserveStatusName(ObserveStatus::Observed), "OBSERVED") == 0,
                  "OBSERVED name");
    fftest::Check(std::strcmp(ObserveStatusName(ObserveStatus::Blocked), "BLOCKED") == 0,
                  "BLOCKED name");
    fftest::Check(std::strcmp(ObserveStatusName(ObserveStatus::Failed), "FAILED") == 0,
                  "FAILED name");
}

void TestHexParsing() {
    auto hex = ParseHexU64(L"0xDEADBEEF");
    fftest::Check(hex.has_value() && *hex == 0xDEADBEEFull, "0x hex parses");
    auto lower = ParseHexU64(L"0xdeadbeef");
    fftest::Check(lower.has_value() && *lower == 0xDEADBEEFull, "lowercase hex parses");
    auto dec = ParseHexU64(L"12345");
    fftest::Check(dec.has_value() && *dec == 12345u, "decimal parses");
    fftest::Check(!ParseHexU64(L"").has_value(), "empty rejects");
    fftest::Check(!ParseHexU64(L"0x").has_value(), "bare prefix rejects");
    fftest::Check(!ParseHexU64(L"0xZZ").has_value(), "non-hex rejects");
    fftest::Check(!ParseHexU64(L"12x34").has_value(), "embedded junk rejects");
    fftest::Check(!ParseHexU64(L"-5").has_value(), "negative rejects");
    fftest::Check(!ParseHexU64(L"0xFFFFFFFFFFFFFFFFF").has_value(), "overflow rejects");
    fftest::Check(!ParseHexU64(L"18446744073709551616").has_value(), "decimal overflow rejects");
    auto max = ParseHexU64(L"0xFFFFFFFFFFFFFFFF");
    fftest::Check(max.has_value() && *max == UINT64_MAX, "u64 max parses");
    fftest::Check(FormatHexU64(0xDEADBEEFull) == "0xDEADBEEF", "hex formats canonical");
    const std::string formatted = FormatHexU64(0x1234ABCDULL);
    auto roundTrip = ParseHexU64(std::wstring(formatted.begin(), formatted.end()));
    fftest::Check(roundTrip.has_value() && *roundTrip == 0x1234ABCDULL, "format/parse round-trips");
}

void TestJoinParentPath() {
    fftest::Check(JoinParentPath(L'C', {L"a", L"b"}) == L"C:\\a\\b", "nested path joins");
    fftest::Check(JoinParentPath(L'D', {L"leaf.txt"}) == L"D:\\leaf.txt", "single leaf joins");
    fftest::Check(JoinParentPath(L'C', {}) == L"C:\\", "empty names yield root");
}

// Builds a synthetic FSCTL_ENUM_USN_DATA buffer: 8-byte cursor + records.
std::vector<uint8_t> MakeEnumBuffer(const std::vector<uint64_t>& frns, size_t trailingBytes = 0,
                                    DWORD trailingRecordLength = 0) {
    std::vector<uint8_t> buffer(sizeof(uint64_t), 0);
    for (const uint64_t frn : frns) {
        USN_RECORD_V2 record{};
        record.RecordLength = sizeof(record);
        record.MajorVersion = 2;
        record.FileReferenceNumber = static_cast<DWORDLONG>(frn);
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&record);
        buffer.insert(buffer.end(), bytes, bytes + sizeof(record));
    }
    if (trailingBytes > 0) {
        const size_t start = buffer.size();
        buffer.resize(start + trailingBytes, 0);
        *reinterpret_cast<DWORD*>(buffer.data() + start) = trailingRecordLength;
    }
    return buffer;
}

void TestWalkEnumBuffer() {
    std::vector<uint64_t> seen;
    auto buffer = MakeEnumBuffer({0x1001, 0x1002, 0x1003});
    const size_t visited =
        WalkEnumBuffer(buffer.data(), buffer.size(), [&](uint64_t frn) { seen.push_back(frn); });
    fftest::Check(visited == 3, "three valid records visited");
    fftest::Check(seen.size() == 3 && seen[0] == 0x1001 && seen[2] == 0x1003,
                  "FRNs delivered in order");

    // Zero-length record stops the walk (production rule).
    std::vector<uint64_t> seenZero;
    auto zeroBuffer = MakeEnumBuffer({0x2001}, sizeof(USN_RECORD_V2), 0);
    const size_t visitedZero = WalkEnumBuffer(zeroBuffer.data(), zeroBuffer.size(),
                                              [&](uint64_t frn) { seenZero.push_back(frn); });
    fftest::Check(visitedZero == 1 && seenZero.size() == 1, "zero-length trailer stops walk");

    // Overrun record stops the walk without over-read.
    std::vector<uint64_t> seenOver;
    auto overBuffer = MakeEnumBuffer({0x3001}, sizeof(USN_RECORD_V2), 0xFFFFFFF0u);
    const size_t visitedOver = WalkEnumBuffer(overBuffer.data(), overBuffer.size(),
                                              [&](uint64_t frn) { seenOver.push_back(frn); });
    fftest::Check(visitedOver == 1 && seenOver.size() == 1, "impossible length stops walk");

    // Degenerate inputs never crash.
    fftest::Check(WalkEnumBuffer(nullptr, 0, [&](uint64_t) {}) == 0, "null buffer yields zero");
    const uint8_t tiny[4] = {0, 0, 0, 0};
    fftest::Check(WalkEnumBuffer(tiny, sizeof(tiny), [&](uint64_t) {}) == 0,
                  "short buffer yields zero");
    std::vector<uint8_t> cursorOnly(sizeof(uint64_t), 0);
    fftest::Check(WalkEnumBuffer(cursorOnly.data(), cursorOnly.size(), [&](uint64_t) {}) == 0,
                  "cursor-only buffer yields zero");
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    TestClassification();
    TestHexParsing();
    TestJoinParentPath();
    TestWalkEnumBuffer();
    return fftest::FailureCount() == 0 ? 0 : 1;
}
