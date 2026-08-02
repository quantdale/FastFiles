#pragma once

#include <cstdint>
#include <string>

namespace ffprotocol {

enum class DiagnosticCategory {
    IndexingError,
    InaccessibleDirectory,
    VolumeStateTransition,
    DatabaseError,
};

// Deliberately metadata-only. There is no content or arbitrary payload field.
struct DiagnosticEvent {
    DiagnosticCategory category = DiagnosticCategory::IndexingError;
    std::wstring path;
    std::wstring volumeId;
    std::wstring state;
    uint32_t errorCode = 0;
    uint64_t itemCount = 0;
    std::wstring outcome;
    std::wstring accountName;
    std::wstring accountSid;
    bool privilegeHeld = false;
    bool privilegeEnabled = false;
    // Candidate-matrix evidence (resolve-raw-volume-privilege-insufficiency
    // task 1.2). These default to empty/zero and are populated only for
    // candidate-matrix rows, so every other diagnostic event serializes
    // byte-identically to before. Existing positional aggregate initializers
    // keep working because trailing members are value-initialized to these
    // same defaults.
    std::wstring candidateId;
    std::wstring privilegeName;
    std::wstring groupContext;
    bool journalQueried = false;
    uint32_t journalQueryError = 0;
    // Result of a minimal FSCTL_READ_USN_JOURNAL probe on the same handle
    // (the "read" half of the journal control/read evidence). Matrix-only.
    bool journalRead = false;
    uint32_t journalReadError = 0;
    uint32_t registrationOrder = 0;
};

std::wstring DiagnosticLogPath();
bool AppendDiagnostic(const DiagnosticEvent& event);

} // namespace ffprotocol
