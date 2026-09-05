#pragma once

#include <cstdint>
#include <string>

namespace ffprotocol {

enum class DiagnosticCategory {
    IndexingError,
    InaccessibleDirectory,
    VolumeStateTransition,
    DatabaseError,
    UiInteraction,
};

// Stable machine-readable identifiers. Keep these values append-only: they
// are consumed by support tooling and monitoring independently of the
// human-readable state/outcome text.
enum class DiagnosticCode : uint32_t {
    FFDIAG_UNKNOWN = 0,
    FFDIAG_CORRUPT_DB = 1,
    FFDIAG_DATABASE_OPEN_FAILED = 2,
    FFDIAG_WATCH_EVICTED = 3,
    FFDIAG_WATCH_EVENT_CREATE_FAILED = 4,
    FFDIAG_WATCH_BUFFER_OVERFLOW = 5,
    FFDIAG_WATCH_GAVE_UP = 6,
    FFDIAG_WATCH_FAILED = 7,
    FFDIAG_SNAPSHOT_PUBLISH_FAILED = 8,
    FFDIAG_FORGET_VOLUME_FAILED = 9,
    FFDIAG_ENGINE_STATUS_CHANGED = 10,
    FFDIAG_DIRECTORY_ACCESS_DENIED = 11,
    FFDIAG_DIRECTORY_NOT_FOUND = 12,
    FFDIAG_DIRECTORY_ENUMERATION_FAILED = 13,
    FFDIAG_ENGINE_DIRECTORY_REQUEST_RECEIVED = 14,
    FFDIAG_ENGINE_UI_SERVER_START_FAILED = 15,
    FFDIAG_ENGINE_PARK_EVENT_FAILED = 16,
    FFDIAG_ENGINE_LAUNCHED = 17,
    FFDIAG_ENGINE_PIPE_NOT_READY = 18,
    FFDIAG_ENGINE_LAUNCH_FAILED = 19,
    FFDIAG_ENGINE_NOT_FOUND = 20,
    FFDIAG_ENGINE_CONNECT_FAILED = 21,
    FFDIAG_ENGINE_CONNECT_SUCCEEDED = 22,
    FFDIAG_ENGINE_UNAVAILABLE = 23,
    FFDIAG_ENGINE_DISCONNECTED = 24,
    FFDIAG_UI_DIRECTORY_REQUEST_QUEUED = 25,
    FFDIAG_UI_DIRECTORY_REQUEST_SENT = 26,
    FFDIAG_UI_SNAPSHOT_PATH_MISS = 27,
    FFDIAG_UI_SNAPSHOT_COLUMN_REFRESH = 28,
    FFDIAG_UI_SNAPSHOT_READ_FAILED = 29,
    FFDIAG_RAW_VOLUME_OPEN = 30,
    FFDIAG_CANDIDATE_MATRIX = 31,
    FFDIAG_STARTUP_TOKEN = 32,
    FFDIAG_PROJECTION_MEMORY_PRESSURE = 33,
    FFDIAG_ENGINE_SIGNATURE_REJECTED = 34,
    FFDIAG_AUTHENTICODE_PINS_UNCONFIGURED = 35,
    // fix-initial-snapshot-handshake: a UI subscription arrived while the
    // server had no committed snapshot generation to acknowledge (internal
    // invariant violation -- UiServer::Start must commit one before the
    // listener opens). Metadata-only, as every diagnostic is.
    FFDIAG_SNAPSHOT_SUBSCRIBE_INVALID = 36,
    // deterministic-missing-path-state D4: one final re-enumeration before
    // a terminal watch error retires the watch, so the path's current
    // state is published instead of silently left stale.
    FFDIAG_WATCH_TERMINAL_REFRESH = 37,
    // deterministic-missing-path-state D2/D3: an indexed merge entry was
    // suppressed because a newer explicit failure observation already
    // exists for the same canonical path.
    FFDIAG_INDEXED_MERGE_SUPPRESSED = 38,
    // active-path-runtime-resilience 4.1: a completion or snapshot
    // notification was rejected because it belonged to an older query/source
    // identity. Append-only so diagnostic consumers retain stable meanings.
    FFDIAG_UI_RESULT_SUPERSEDED = 39,
    // interactive-ui-runtime-determinism: opt-in, metadata-only command
    // dispatch evidence for isolated UI automation sessions.
    FFDIAG_UI_COMMAND_DISPATCHED = 40,
    // interactive-ui-runtime-determinism: opt-in provider child-query evidence
    // for distinguishing a stale UIA root from a wrong live pane count.
    FFDIAG_UI_PROVIDER_CHILD_QUERY = 41,
    // index-durability-false-success-closure: the service reported a scan
    // that could not run to completion (ScanFailed terminal received).
    FFDIAG_SCAN_FAILED = 42,
    // A scan terminal (complete or failed) arrived for a volume with no
    // scan in flight -- a duplicate, stale, or out-of-model message whose
    // authority the engine refused.
    FFDIAG_STALE_SCAN_TERMINAL = 43,
    // A scan batch could not be applied; the volume's cursor is frozen at
    // its last-applied position and the pending completion is non-authoritative.
    FFDIAG_SCAN_BATCH_APPLY_FAILED = 44,
    // A reconciliation request arrived while a scan was already in flight
    // for the volume and was deferred to that scan's authoritative
    // completion instead of racing it (index-durability-false-success-closure D4).
    FFDIAG_RECONCILIATION_DEFERRED = 45,
    // Startup integrity_check could not reach a verdict (lock/IO/busy).
    // The database was preserved and startup retried instead of deleting
    // (index-durability-false-success-closure D6).
    FFDIAG_DB_INTEGRITY_INCONCLUSIVE = 46,
};

const wchar_t* DiagnosticCodeName(DiagnosticCode code);

enum class DiagnosticFormat {
    Text,
    Json,
};

// Alias the descriptive name used by callers that select a stream format.
using DiagnosticOutputFormat = DiagnosticFormat;

// Deliberately metadata-only. There is no content or arbitrary payload field.
struct DiagnosticEvent {
    DiagnosticCategory category = DiagnosticCategory::IndexingError;
    DiagnosticCode code = DiagnosticCode::FFDIAG_UNKNOWN;
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
    // Authenticode leaf thumbprint observed during engine discovery. This is
    // metadata-only and remains empty for unsigned candidates.
    std::wstring signatureThumbprint;
};

std::wstring DiagnosticLogPath();
std::wstring SerializeDiagnosticEvent(const DiagnosticEvent& event,
                                      DiagnosticFormat format = DiagnosticFormat::Text);
bool AppendDiagnostic(const DiagnosticEvent& event,
                      DiagnosticFormat format = DiagnosticFormat::Text);

// settings-and-appearance 8.3/8.4: diagnostic bundle export for sharing with
// support. By default the bundle is aggregated/redacted -- per-category and
// per-error-code counts plus directory-structure summaries, never literal
// paths or filenames. Passing includeLiteralPaths=true is the explicit
// opt-in that includes literal paths; file content is excluded in both
// modes (the log itself never holds content).
//
// Writes the bundle to `destinationPath` and returns true on success.
bool ExportDiagnosticBundle(const std::wstring& destinationPath, bool includeLiteralPaths);

} // namespace ffprotocol
