#include <cstdio>
#include <cstring>
#include <vector>

#include "MftParser.h"

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

void WriteU16(std::vector<uint8_t>& buf, size_t offset, uint16_t v) { std::memcpy(buf.data() + offset, &v, 2); }
void WriteU32(std::vector<uint8_t>& buf, size_t offset, uint32_t v) { std::memcpy(buf.data() + offset, &v, 4); }
void WriteU64(std::vector<uint8_t>& buf, size_t offset, uint64_t v) { std::memcpy(buf.data() + offset, &v, 8); }

// Builds a synthetic, well-formed 1024-byte MFT record with a
// $STANDARD_INFORMATION and a $FILE_NAME attribute, including a correct
// Update Sequence Array, matching the on-disk layout MftParser.cpp
// expects. Used to validate the fixup algorithm and attribute-walking
// logic against a known-good input (and deliberately corrupted variants)
// without requiring an actual NTFS volume.
std::vector<uint8_t> BuildSyntheticRecord(uint16_t bytesPerSector, const std::u16string& name,
                                           uint64_t parentRef, uint64_t size, bool directory) {
    constexpr size_t kRecordSize = 1024;
    std::vector<uint8_t> buf(kRecordSize, 0);

    const size_t sectorCount = kRecordSize / bytesPerSector;
    const uint16_t usaSize = static_cast<uint16_t>(sectorCount + 1);
    const uint16_t usaOffset = 0x30; // arbitrary, after the fixed header region
    const uint16_t firstAttributeOffset = static_cast<uint16_t>(usaOffset + usaSize * 2 + 6 /* pad */);

    std::memcpy(buf.data(), "FILE", 4);
    WriteU16(buf, 4, usaOffset);
    WriteU16(buf, 6, usaSize);
    WriteU16(buf, 20, firstAttributeOffset);
    WriteU16(buf, 22, static_cast<uint16_t>(0x0001 | (directory ? 0x0002 : 0)));

    // $STANDARD_INFORMATION (resident, type 0x10)
    size_t off = firstAttributeOffset;
    const size_t stdInfoValueLen = 48;
    const size_t stdInfoRecLen = 24 + stdInfoValueLen;
    WriteU32(buf, off + 0, 0x10);
    WriteU32(buf, off + 4, static_cast<uint32_t>(stdInfoRecLen));
    buf[off + 8] = 0; // resident
    WriteU32(buf, off + 16, static_cast<uint32_t>(stdInfoValueLen));
    WriteU16(buf, off + 20, 24);
    WriteU64(buf, off + 24 + 0, 111);  // creation time
    WriteU64(buf, off + 24 + 8, 222);  // last modified
    WriteU64(buf, off + 24 + 16, 333); // last change (unused)
    WriteU64(buf, off + 24 + 24, 444); // last access
    WriteU32(buf, off + 24 + 32, 0x20); // FILE_ATTRIBUTE_ARCHIVE
    off += stdInfoRecLen;

    // $FILE_NAME (resident, type 0x30)
    const size_t fileNameValueLen = 66 + name.size() * 2;
    const size_t fileNameRecLen = 24 + fileNameValueLen;
    WriteU32(buf, off + 0, 0x30);
    WriteU32(buf, off + 4, static_cast<uint32_t>(fileNameRecLen));
    buf[off + 8] = 0;
    WriteU32(buf, off + 16, static_cast<uint32_t>(fileNameValueLen));
    WriteU16(buf, off + 20, 24);
    WriteU64(buf, off + 24 + 0, parentRef);
    WriteU64(buf, off + 24 + 48, size);
    buf[off + 24 + 64] = static_cast<uint8_t>(name.size());
    buf[off + 24 + 65] = 1; // Win32 namespace
    std::memcpy(buf.data() + off + 24 + 66, name.data(), name.size() * 2);
    off += fileNameRecLen;

    // End marker
    WriteU32(buf, off, 0xFFFFFFFFu);
    off += 8;

    WriteU32(buf, 24, static_cast<uint32_t>(off)); // BytesInUse

    // Update Sequence Array: entry[0] = USN marker, entry[1..N] = the real
    // last-2-bytes of each sector, which we now overwrite on-disk with the
    // USN marker (simulating what NTFS itself does before persisting).
    const uint16_t usn = 7;
    WriteU16(buf, usaOffset, usn);
    for (size_t sector = 0; sector < sectorCount; ++sector) {
        const size_t tailOffset = (sector + 1) * bytesPerSector - 2;
        uint16_t trueValue;
        std::memcpy(&trueValue, buf.data() + tailOffset, 2);
        WriteU16(buf, usaOffset + (sector + 1) * 2, trueValue);
        WriteU16(buf, tailOffset, usn);
    }

    return buf;
}

void TestWellFormedRecordParsesCorrectly() {
    auto record = BuildSyntheticRecord(512, u"hello.txt", 0x0005000000000005ULL, 12345, false);
    Check(ApplyFixupAndValidate(record.data(), record.size(), 512) == FixupResult::Ok,
          "a well-formed record passes fixup and validation");

    auto parsed = ParseMftAttributes(record.data(), record.size());
    Check(parsed.has_value(), "a well-formed record parses successfully");
    Check(parsed->fileName == u"hello.txt", "the $FILE_NAME name round-trips");
    Check(parsed->parentFileReferenceNumber == 0x0005000000000005ULL, "the parent FRN round-trips");
    Check(parsed->sizeBytes == 12345, "the $FILE_NAME RealSize round-trips as sizeBytes");
    Check(parsed->creationTime == 111, "creation time round-trips");
    Check(parsed->lastModifiedTime == 222, "last-modified time round-trips");
    Check(parsed->lastAccessTime == 444, "last-access time round-trips");
    Check((parsed->fileAttributes & 0x20) != 0, "the archive attribute bit round-trips");
    Check((parsed->fileAttributes & 0x10) == 0, "a non-directory record does not get the directory bit synthesized");
}

void TestDirectoryFlagIsSynthesizedFromRecordHeader() {
    auto record = BuildSyntheticRecord(512, u"subdir", 5, 0, true);
    Check(ApplyFixupAndValidate(record.data(), record.size(), 512) == FixupResult::Ok, "a directory record passes validation");
    auto parsed = ParseMftAttributes(record.data(), record.size());
    Check(parsed.has_value() && (parsed->fileAttributes & 0x10) != 0,
          "the record header's directory flag is merged into fileAttributes ($STANDARD_INFORMATION never sets it)");
}

void TestNotInUseRecordIsRejectedBeforeParsing() {
    auto record = BuildSyntheticRecord(512, u"hello.txt", 5, 0, false);
    record[22] = 0; // clear the in-use bit of Flags (low byte)
    Check(ApplyFixupAndValidate(record.data(), record.size(), 512) == FixupResult::NotInUse,
          "a free/deleted record is rejected by its in-use flag before any attribute is read");
}

void TestWrongSignatureIsRejected() {
    auto record = BuildSyntheticRecord(512, u"hello.txt", 5, 0, false);
    record[0] = 'X';
    Check(ApplyFixupAndValidate(record.data(), record.size(), 512) == FixupResult::NotAFileRecord,
          "a buffer without the \"FILE\" signature is rejected outright");
}

void TestTornSectorIsDetectedAsMalformed() {
    auto record = BuildSyntheticRecord(512, u"hello.txt", 5, 0, false);
    record[510] ^= 0xFF; // corrupt the marker NTFS would have written at the first sector's tail
    Check(ApplyFixupAndValidate(record.data(), record.size(), 512) == FixupResult::Malformed,
          "a fixup marker mismatch (torn write) is detected rather than silently trusted");
}

// tasks.md 9.8: a record with a resident $DATA attribute (small/resident
// file content, exactly the case design.md calls out as the trap -- "even
// for small files whose content is resident inside the MFT record
// itself") must still parse correctly using only $STANDARD_INFORMATION/
// $FILE_NAME, with the $DATA bytes never reaching ParsedMftRecord (whose
// fields structurally have no place to carry arbitrary attribute payload
// in the first place).
void TestResidentDataAttributeIsNeverSurfaced() {
    constexpr size_t kRecordSize = 1024;
    std::vector<uint8_t> buf(kRecordSize, 0);
    constexpr uint16_t bytesPerSector = 512;
    const size_t sectorCount = kRecordSize / bytesPerSector;
    const uint16_t usaSize = static_cast<uint16_t>(sectorCount + 1);
    const uint16_t usaOffset = 0x30;
    const uint16_t firstAttributeOffset = static_cast<uint16_t>(usaOffset + usaSize * 2 + 6);

    std::memcpy(buf.data(), "FILE", 4);
    WriteU16(buf, 4, usaOffset);
    WriteU16(buf, 6, usaSize);
    WriteU16(buf, 20, firstAttributeOffset);
    WriteU16(buf, 22, 0x0001);

    size_t off = firstAttributeOffset;
    // $STANDARD_INFORMATION
    WriteU32(buf, off + 0, 0x10);
    WriteU32(buf, off + 4, 24 + 48);
    buf[off + 8] = 0;
    WriteU32(buf, off + 16, 48);
    WriteU16(buf, off + 20, 24);
    WriteU64(buf, off + 24 + 32, 0x20);
    off += 24 + 48;

    // $FILE_NAME
    const std::u16string name = u"secret.txt";
    const size_t fileNameValueLen = 66 + name.size() * 2;
    WriteU32(buf, off + 0, 0x30);
    WriteU32(buf, off + 4, static_cast<uint32_t>(24 + fileNameValueLen));
    buf[off + 8] = 0;
    WriteU32(buf, off + 16, static_cast<uint32_t>(fileNameValueLen));
    WriteU16(buf, off + 20, 24);
    WriteU64(buf, off + 24 + 0, 5);
    buf[off + 24 + 64] = static_cast<uint8_t>(name.size());
    buf[off + 24 + 65] = 1;
    std::memcpy(buf.data() + off + 24 + 66, name.data(), name.size() * 2);
    off += 24 + fileNameValueLen;

    // $DATA (resident, type 0x80) -- a small file's content stored inline.
    const char secretContent[] = "SECRET_FILE_CONTENT_MUST_NEVER_BE_FORWARDED";
    const size_t dataValueLen = sizeof(secretContent);
    WriteU32(buf, off + 0, 0x80);
    WriteU32(buf, off + 4, static_cast<uint32_t>(24 + dataValueLen));
    buf[off + 8] = 0; // resident -- exactly the "small file" case
    WriteU32(buf, off + 16, static_cast<uint32_t>(dataValueLen));
    WriteU16(buf, off + 20, 24);
    std::memcpy(buf.data() + off + 24, secretContent, dataValueLen);
    off += 24 + dataValueLen;

    WriteU32(buf, off, 0xFFFFFFFFu);
    off += 8;
    WriteU32(buf, 24, static_cast<uint32_t>(off));

    const uint16_t usn = 7;
    WriteU16(buf, usaOffset, usn);
    for (size_t sector = 0; sector < sectorCount; ++sector) {
        const size_t tailOffset = (sector + 1) * bytesPerSector - 2;
        uint16_t trueValue;
        std::memcpy(&trueValue, buf.data() + tailOffset, 2);
        WriteU16(buf, usaOffset + (sector + 1) * 2, trueValue);
        WriteU16(buf, tailOffset, usn);
    }

    Check(ApplyFixupAndValidate(buf.data(), buf.size(), bytesPerSector) == FixupResult::Ok,
          "a record containing a $DATA attribute still passes fixup/validation");
    auto parsed = ParseMftAttributes(buf.data(), buf.size());
    Check(parsed.has_value() && parsed->fileName == u"secret.txt",
          "$STANDARD_INFORMATION/$FILE_NAME are still parsed correctly alongside an ignored $DATA attribute");
    // ParsedMftRecord has no field capable of carrying arbitrary
    // attribute payload -- this is a structural guarantee (the struct has
    // exactly the allowlisted fields and nothing else), confirmed here by
    // checking the one variable-length field it does have (fileName)
    // never picked up any fragment of the $DATA content.
    Check(parsed->fileName.find(u"SECRET") == std::u16string::npos,
          "no fragment of the $DATA content appears in the parsed record's name field");
}

void TestOversizedAttributeLengthIsRejectedNotOverread() {
    auto record = BuildSyntheticRecord(512, u"hello.txt", 5, 0, false);
    Check(ApplyFixupAndValidate(record.data(), record.size(), 512) == FixupResult::Ok, "fixup succeeds before corrupting the attribute stream");
    // firstAttributeOffset matches BuildSyntheticRecord's own formula for
    // these parameters (usaOffset=0x30, sectorCount=1024/512=2, usaSize=3).
    constexpr size_t kFirstAttributeOffset = 0x30 + 3 * 2 + 6;
    WriteU32(record, kFirstAttributeOffset + 4, 0xFFFFFFFFu); // absurd RecordLength
    auto parsed = ParseMftAttributes(record.data(), record.size());
    Check(!parsed.has_value(), "an attribute whose declared length overruns the record is rejected, not read out of bounds");
}

} // namespace

int main() {
    TestWellFormedRecordParsesCorrectly();
    TestDirectoryFlagIsSynthesizedFromRecordHeader();
    TestNotInUseRecordIsRejectedBeforeParsing();
    TestWrongSignatureIsRejected();
    TestTornSectorIsDetectedAsMalformed();
    TestResidentDataAttributeIsNeverSurfaced();
    TestOversizedAttributeLengthIsRejectedNotOverread();

    if (g_failures > 0) {
        std::fprintf(stderr, "%d test(s) failed\n", g_failures);
        return 1;
    }
    std::printf("All tests passed.\n");
    return 0;
}
