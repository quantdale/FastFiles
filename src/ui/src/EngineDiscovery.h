#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ffsetup/PinnedSignatures.h"

namespace ffui {

// recovery-degraded-browsing D1: deterministic engine executable discovery
// for the UI's "lazy start" (EngineClient). Candidates are derived only from
// the UI's own location and the fixed install location -- never a PATH
// lookup or a disk-wide search -- so a rogue binary in an arbitrary
// directory can never be picked up.
//
// Priority order:
//   1. Same directory as the UI executable (installed layout: all
//      executables staged together under the ACL-locked install dir).
//   2. Sibling "engine" directory of the UI's parent (development layout:
//      build/<config>/src/ui -> build/<config>/src/engine), normalized so
//      embedded ".." can never escape the derived tree.
//   3. %ProgramFiles%\FastFiles (installer layout).
std::vector<std::wstring> EngineDiscoveryCandidates(const std::wstring& uiExecutablePath);

// True when `path` names an existing regular file (not a directory).
bool IsValidEngineExecutable(const std::wstring& path);

enum class EngineExecutableValidationStatus : uint8_t {
    Accepted,
    NotRegularFile,
    CanonicalPathRejected,
    Unsigned,
    SignatureMismatch,
    PinsUnconfigured,
};

struct EngineExecutableValidation {
    EngineExecutableValidationStatus status = EngineExecutableValidationStatus::NotRegularFile;
    std::wstring canonicalPath;
    std::optional<ffsetup::Thumbprint> signerThumbprint;

    bool Accepted() const noexcept {
        return status == EngineExecutableValidationStatus::Accepted;
    }
};

// Inspects a candidate immediately before launch. The candidate must remain a
// regular, non-reparse file whose final path is the same as the requested
// path, and its Authenticode signer must match the engine pin. A diagnostic
// build may explicitly accept an unsigned candidate through the existing
// compile-time-only ffsetup override; shipped configurations remain
// fail-closed.
EngineExecutableValidation ValidateEngineExecutable(const std::wstring& path);

const wchar_t* EngineExecutableValidationStatusName(EngineExecutableValidationStatus status) noexcept;

} // namespace ffui
