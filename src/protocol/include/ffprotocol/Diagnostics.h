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
};

std::wstring DiagnosticLogPath();
bool AppendDiagnostic(const DiagnosticEvent& event);

} // namespace ffprotocol
