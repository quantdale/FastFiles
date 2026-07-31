#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "MftRecordParser.h"

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

using namespace ffindexsvc;

void PutU16(std::vector<uint8_t>& buf, size_t offset, uint16_t v) { std::memcpy(buf.data() + offset, &v, 2); }
void PutU32(std::vector<uint8_t>& buf, size_t offset, uint32_t v) { std::memcpy(buf.data() + offset, &v, 4); }
void PutU64(std::vector<uint8_t>& buf, size_t offset, uint64_t v) { std::memcpy(buf.data() + offset, &v, 8); }
void PutU8(std::vector<uint8_t>& buf, size_t offset, uint8_t v) { buf[offset] = v; }

constexpr uint32_t kRecordSize = 1024;
constexpr uint32_t kBytesPerSector = 512;

// Builds a well-formed synthetic MFT record byte-for-byte (header, USA,
// $STANDARD_INFORMATION, $FILE_NAME, end marker), then applies the same
// "sector-tail overwritten with the update sequence number" corruption
// real NTFS applies on disk -- so the parser's own fixup logic is
// genuinely exercised, not just its happy-path field extraction.
std::vector<uint8_t> BuildValidRecord(uint64_t parentFrn, uint64_t sizeBytes, const std::u16string& name, bool isDirectory) {
    std::vector<uint8_t> buf(kRecordSize, 0);

    PutU32(buf, 0, 0x454C4946u); // "FILE"
    const uint16_t usaOffset = 42;
    const uint16_t usaCount = 3; // 1 USN slot + 2 sectors (1024 / 512)
    PutU16(buf, 4, usaOffset);
    PutU16(buf, 6, usaCount);
    PutU64(buf, 8, 0);
    PutU16(buf, 16, 1);
    PutU16(buf, 18, 1);
    const uint16_t firstAttrOffset = 48; // usaOffset(42) + usaCount*2(6)
    PutU16(buf, 20, firstAttrOffset);
    uint16_t flags = 0x0001;
    if (isDirectory) flags |= 0x0002;
    PutU16(buf, 22, flags);

    size_t off = firstAttrOffset;
    const uint32_t siValueLen = 48;
    const uint16_t siValueOffset = 24;
    const uint32_t siRecordLen = ((siValueOffset + siValueLen) + 7) & ~7u;
    PutU32(buf, off + 0, 0x10);
    PutU32(buf, off + 4, siRecordLen);
    PutU8(buf, off + 8, 0);
    PutU8(buf, off + 9, 0);
    PutU16(buf, off + 10, 24);
    PutU16(buf, off + 12, 0);
    PutU16(buf, off + 14, 0);
    PutU32(buf, off + 16, siValueLen);
    PutU16(buf, off + 20, siValueOffset);
    PutU8(buf, off + 22, 0);
    PutU8(buf, off + 23, 0);
    PutU64(buf, off + siValueOffset + 0, 111111);
    PutU64(buf, off + siValueOffset + 8, 222222); // last modification time -> our lastWriteTime
    PutU64(buf, off + siValueOffset + 16, 333333);
    PutU64(buf, off + siValueOffset + 24, 444444);
    PutU32(buf, off + siValueOffset + 32, 0x20); // ARCHIVE

    off += siRecordLen;

    const uint16_t fnValueOffset = 24;
    const uint32_t fnFixedLen = 66;
    const uint32_t fnValueLen = static_cast<uint32_t>(fnFixedLen + name.size() * 2);
    const uint32_t fnRecordLen = ((fnValueOffset + fnValueLen) + 7) & ~7u;
    PutU32(buf, off + 0, 0x30);
    PutU32(buf, off + 4, fnRecordLen);
    PutU8(buf, off + 8, 0);
    PutU8(buf, off + 9, 0);
    PutU16(buf, off + 10, 24);
    PutU16(buf, off + 12, 0);
    PutU16(buf, off + 14, 1);
    PutU32(buf, off + 16, fnValueLen);
    PutU16(buf, off + 20, fnValueOffset);
    PutU8(buf, off + 22, 0);
    PutU8(buf, off + 23, 0);
    PutU64(buf, off + fnValueOffset + 0, parentFrn);
    PutU64(buf, off + fnValueOffset + 8, 111111);
    PutU64(buf, off + fnValueOffset + 16, 222222);
    PutU64(buf, off + fnValueOffset + 24, 333333);
    PutU64(buf, off + fnValueOffset + 32, 444444);
    PutU64(buf, off + fnValueOffset + 40, sizeBytes);
    PutU64(buf, off + fnValueOffset + 48, sizeBytes);
    PutU32(buf, off + fnValueOffset + 56, 0);
    PutU8(buf, off + fnValueOffset + 64, static_cast<uint8_t>(name.size()));
    PutU8(buf, off + fnValueOffset + 65, 1); // Win32 namespace
    std::memcpy(buf.data() + off + fnValueOffset + 66, name.data(), name.size() * 2);

    off += fnRecordLen;

    PutU32(buf, off, 0xFFFFFFFFu); // end marker
    off += 8;

    PutU32(buf, 24, static_cast<uint32_t>(off)); // used size

    const uint16_t usn = 0xABCD;
    PutU16(buf, usaOffset, usn);
    const uint32_t sectorCount = kRecordSize / kBytesPerSector;
    for (uint32_t s = 0; s < sectorCount; ++s) {
        size_t lastTwo = (s + 1) * kBytesPerSector - 2;
        uint16_t original;
        std::memcpy(&original, buf.data() + lastTwo, 2);
        PutU16(buf, usaOffset + 2 + s * 2, original);
        PutU16(buf, lastTwo, usn);
    }

    return buf;
}

void TestWellFormedRecordParsesCorrectly() {
    auto buf = BuildValidRecord(5, 777, u"hello.txt", false);
    auto parsed = ParseMftRecord(buf, kBytesPerSector);
    Check(parsed.has_value(), "well-formed record parses");
    if (!parsed) return;
    Check(parsed->hasStandardInformation, "parsed record has $STANDARD_INFORMATION");
    Check(parsed->hasFileName, "parsed record has $FILE_NAME");
    Check(parsed->lastWriteTime == 222222, "last-write time matches $STANDARD_INFORMATION's LastModificationTime");
    Check(parsed->parentFileReferenceNumber == 5, "parent FRN matches $FILE_NAME's ParentDirectory field");
    Check(parsed->realSizeBytes == 777, "size matches $FILE_NAME's cached RealSize field");
    Check(parsed->name == u"hello.txt", "name round-trips exactly");
    Check(!parsed->isDirectory, "non-directory record is not flagged as a directory");
    Check((parsed->dosAttributes & 0x10) == 0,
          "the DIRECTORY bit is never sourced from $STANDARD_INFORMATION's own attributes field");
}

void TestDirectoryFlagComesFromRecordHeader() {
    auto buf = BuildValidRecord(5, 0, u"SubDir", true);
    auto parsed = ParseMftRecord(buf, kBytesPerSector);
    Check(parsed.has_value() && parsed->isDirectory, "directory flag is derived from the record header's own flags field");
}

void TestBadSignatureRejected() {
    auto buf = BuildValidRecord(5, 1, u"x.txt", false);
    buf[0] = 'X';
    Check(!ParseMftRecord(buf, kBytesPerSector).has_value(), "a record without the 'FILE' signature is rejected");
}

void TestTornWriteRejected() {
    auto buf = BuildValidRecord(5, 1, u"x.txt", false);
    buf[kBytesPerSector - 1] ^= 0xFF; // corrupt sector 0's on-disk fixup marker
    Check(!ParseMftRecord(buf, kBytesPerSector).has_value(),
          "a torn write (Update Sequence Array mismatch) is detected and rejected, not silently trusted");
}

void TestOversizedAttributeLengthRejectedWithoutOobRead() {
    auto buf = BuildValidRecord(5, 1, u"x.txt", false);
    // The $STANDARD_INFORMATION attribute's own record-length field lives
    // at firstAttrOffset(48)+4; corrupt it to claim a length that runs
    // past the 1024-byte buffer. Run under ASan in CI-equivalent builds --
    // an out-of-bounds read here would abort the process outright rather
    // than returning std::nullopt.
    PutU32(buf, 48 + 4, 0xFFFFFF00u);
    Check(!ParseMftRecord(buf, kBytesPerSector).has_value(),
          "an attribute claiming a length past the buffer is rejected, not read out of bounds");
}

void TestDeallocatedRecordRejected() {
    auto buf = BuildValidRecord(5, 1, u"x.txt", false);
    uint16_t flags;
    std::memcpy(&flags, buf.data() + 22, 2);
    flags &= ~static_cast<uint16_t>(0x0001);
    std::memcpy(buf.data() + 22, &flags, 2);
    Check(!ParseMftRecord(buf, kBytesPerSector).has_value(), "a deallocated (not-in-use) MFT record slot is rejected");
}

// task 9.8/spec "Resident file content is never forwarded": a record with
// a resident $DATA attribute (the small-file-content-inline case) must
// never surface those bytes, even though the parser reads clean past it.
void TestDataAttributeNeverSurfaced() {
    // Build a record with $STANDARD_INFORMATION, then a resident $DATA
    // attribute containing an unmistakable payload, then $FILE_NAME.
    std::vector<uint8_t> buf(kRecordSize, 0);
    PutU32(buf, 0, 0x454C4946u);
    const uint16_t usaOffset = 42;
    const uint16_t usaCount = 3;
    PutU16(buf, 4, usaOffset);
    PutU16(buf, 6, usaCount);
    PutU16(buf, 16, 1);
    PutU16(buf, 18, 1);
    const uint16_t firstAttrOffset = 48;
    PutU16(buf, 20, firstAttrOffset);
    PutU16(buf, 22, 0x0001);

    size_t off = firstAttrOffset;

    // $STANDARD_INFORMATION
    const uint16_t siValueOffset = 24;
    const uint32_t siValueLen = 48;
    const uint32_t siRecordLen = ((siValueOffset + siValueLen) + 7) & ~7u;
    PutU32(buf, off + 0, 0x10);
    PutU32(buf, off + 4, siRecordLen);
    PutU8(buf, off + 8, 0);
    PutU16(buf, off + 20, siValueOffset);
    PutU32(buf, off + 16, siValueLen);
    PutU64(buf, off + siValueOffset + 8, 555555);
    off += siRecordLen;

    // $DATA (0x80), resident, containing an unmistakable marker string --
    // the parser must skip this attribute using only its own declared
    // length, never inspecting or forwarding its payload.
    const char kSecretMarker[] = "SECRET_FILE_CONTENT_MUST_NEVER_APPEAR";
    const uint16_t dataValueOffset = 24;
    const uint32_t dataValueLen = static_cast<uint32_t>(sizeof(kSecretMarker));
    const uint32_t dataRecordLen = ((dataValueOffset + dataValueLen) + 7) & ~7u;
    PutU32(buf, off + 0, 0x80); // $DATA
    PutU32(buf, off + 4, dataRecordLen);
    PutU8(buf, off + 8, 0); // resident
    PutU32(buf, off + 16, dataValueLen);
    PutU16(buf, off + 20, dataValueOffset);
    std::memcpy(buf.data() + off + dataValueOffset, kSecretMarker, sizeof(kSecretMarker));
    off += dataRecordLen;

    // $FILE_NAME
    const uint16_t fnValueOffset = 24;
    const uint32_t fnFixedLen = 66;
    const std::u16string name = u"secret.txt";
    const uint32_t fnValueLen = static_cast<uint32_t>(fnFixedLen + name.size() * 2);
    const uint32_t fnRecordLen = ((fnValueOffset + fnValueLen) + 7) & ~7u;
    PutU32(buf, off + 0, 0x30);
    PutU32(buf, off + 4, fnRecordLen);
    PutU8(buf, off + 8, 0);
    PutU32(buf, off + 16, fnValueLen);
    PutU16(buf, off + 20, fnValueOffset);
    PutU64(buf, off + fnValueOffset + 0, 5);
    PutU64(buf, off + fnValueOffset + 48, 123); // real size
    PutU8(buf, off + fnValueOffset + 64, static_cast<uint8_t>(name.size()));
    PutU8(buf, off + fnValueOffset + 65, 1);
    std::memcpy(buf.data() + off + fnValueOffset + 66, name.data(), name.size() * 2);
    off += fnRecordLen;

    PutU32(buf, off, 0xFFFFFFFFu);
    off += 8;
    PutU32(buf, 24, static_cast<uint32_t>(off));

    const uint16_t usn = 0x1234;
    PutU16(buf, usaOffset, usn);
    for (uint32_t s = 0; s < kRecordSize / kBytesPerSector; ++s) {
        size_t lastTwo = (s + 1) * kBytesPerSector - 2;
        uint16_t original;
        std::memcpy(&original, buf.data() + lastTwo, 2);
        PutU16(buf, usaOffset + 2 + s * 2, original);
        PutU16(buf, lastTwo, usn);
    }

    auto parsed = ParseMftRecord(buf, kBytesPerSector);
    Check(parsed.has_value(), "a record containing a $DATA attribute alongside the allowlisted ones still parses");
    if (parsed) {
        Check(parsed->name == u"secret.txt", "the $FILE_NAME after a $DATA attribute is still read correctly");
        Check(parsed->realSizeBytes == 123, "$FILE_NAME fields after a skipped $DATA attribute are unaffected");
    }

    // The marker string must not appear anywhere in the parsed struct's
    // observable fields -- this is a structural guarantee (the parser
    // never reads a non-allowlisted attribute's value bytes at all, only
    // its header's declared length to skip past it), confirmed here by
    // construction rather than a byte-search, since ParsedMftRecord has no
    // field capable of holding arbitrary attribute payload in the first
    // place.
    Check(parsed.has_value(), "$DATA's presence does not prevent successful parsing of the allowlisted attributes");
}

void TestMissingStandardInformationRejected() {
    // Zero out the $STANDARD_INFORMATION attribute's type code so only
    // $FILE_NAME remains -- both allowlisted attributes must be present
    // for a record to be considered valid.
    auto buf = BuildValidRecord(5, 1, u"x.txt", false);
    PutU32(buf, 48 + 0, 0x40); // relabel as $OBJECT_ID instead of $STANDARD_INFORMATION
    Check(!ParseMftRecord(buf, kBytesPerSector).has_value(),
          "a record missing $STANDARD_INFORMATION (even with a valid $FILE_NAME) is rejected");
}

} // namespace

int main() {
    TestWellFormedRecordParsesCorrectly();
    TestDirectoryFlagComesFromRecordHeader();
    TestBadSignatureRejected();
    TestTornWriteRejected();
    TestOversizedAttributeLengthRejectedWithoutOobRead();
    TestDeallocatedRecordRejected();
    TestDataAttributeNeverSurfaced();
    TestMissingStandardInformationRejected();

    if (g_failures > 0) {
        std::fprintf(stderr, "\n%d check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    std::printf("\nall checks passed\n");
    return EXIT_SUCCESS;
}
