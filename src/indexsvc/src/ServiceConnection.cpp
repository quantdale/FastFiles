#include "ServiceConnection.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "ffipc/PipeFraming.h"
#include "ffprotocol/Diagnostics.h"
#include "ffprotocol/Version.h"

#include "ClientAuthentication.h"
#include "CommandSurface.h"
#include "DiagnosticsHardening.h"
#include "ScanWorkerBoundary.h"
#include "ServiceWorkerLifecycle.h"
#include "StalenessMonitor.h"
#include "UsnJournalReader.h"
#include "VolumeEnumeration.h"
#include "VolumeScanner.h"
#include "WriteDeadline.h"

namespace ffindexsvc {

namespace {

using ffprotocol::MessageType;
using detail::ActiveWorker;
using detail::StopAndJoin;
using detail::StopAndJoinAll;

// A client that stops sending frames would otherwise pin this connection --
// and any scan/journal worker threads it owns -- indefinitely. Conservative:
// far beyond the engine's ~20s heartbeat cadence, so only genuinely silent
// connections are reaped (the timeout closes via the same teardown path as
// any other disconnect).
constexpr std::chrono::milliseconds kInboundIdleTimeout{300000};
// hardening-campaign Phase 7 H1: authorization validity on an ESTABLISHED
// connection is re-proven on this wall-clock cadence -- never driven by which
// message types a client sends. Matches the engine's ~20s heartbeat cycle
// (the honest-path rate, so behavior is never weaker than before) and stays an
// order of magnitude under kInboundIdleTimeout so the cadence always fires
// before the reap.
constexpr std::chrono::milliseconds kRevalidationInterval{20000};
// Heartbeat may skip its OWN revalidation only when the cadence gate ran
// within this epsilon on the very same frame -- prevents back-to-back
// WinVerifyTrust while preserving event-driven re-proof on the honest path.
constexpr std::chrono::milliseconds kRevalidationFreshEpsilon{100};
constexpr std::chrono::milliseconds kHandshakeTimeout{30000};

// The service listener opens Ctrl-pipe instances with FILE_FLAG_OVERLAPPED.
// ReadFrameOverlappedDetailed issues one bounded ReadFile for the header and
// payload, then waits on the operation event instead of polling pipe state.
// If the absolute deadline expires, the operation is cancelled and the
// connection follows the existing reject-by-close path.
//
// shutdownEvent (the listener's manual-reset stop signal) is waited on
// alongside the read, so a listener Stop() releases a reader that is parked
// between frames -- where CancelIoEx is a no-op -- rather than letting the
// loop re-arm a fresh kInboundIdleTimeout read and stall the listener's
// JoinConnectionThreads() for up to 300 s. A null event degrades to the
// deadline-bounded read alone.
std::optional<ffipc::ReceivedFrame> ReadActiveFrame(HANDLE pipeHandle, HANDLE shutdownEvent,
                                                    std::chrono::steady_clock::time_point idleDeadline) {
    ffipc::ReceivedFrame frameOut;
    const ffipc::FrameReadStatus status =
        ffipc::ReadFrameOverlappedDetailed(pipeHandle, shutdownEvent, idleDeadline, &frameOut);
    if (status != ffipc::FrameReadStatus::Success) {
        // Includes Cancelled (listener shutdown), the idle-deadline timeout,
        // clean disconnect, and I/O error -- every one ends this connection.
        return std::nullopt;
    }
    return frameOut;
}

// index-storage-and-scanning: a scan or journal stream runs on its own
// background thread (writes to the shared pipe handle serialized via
// `writeMutex`) so the ctrl loop below stays free to keep servicing
// Heartbeat/Stop/Close/second-scan requests concurrently, per tasks.md
// 4.4/5.4's "batched streaming ... for the duration of an open journal
// handle" requirement.
bool SendIncompatibleVersion(HANDLE pipeHandle) {
    ffprotocol::IncompatibleVersionPayload payload{ffprotocol::kCurrentProtocolVersion};
    return WriteFrameWithDeadline(pipeHandle, static_cast<uint16_t>(MessageType::IncompatibleVersion), &payload, sizeof(payload));
}

bool SendHandshakeReject(HANDLE pipeHandle, ffprotocol::HandshakeRejectReason reason) {
    ffprotocol::HandshakeRejectPayload payload{reason};
    return WriteFrameWithDeadline(pipeHandle, static_cast<uint16_t>(MessageType::HandshakeReject), &payload, sizeof(payload));
}

void AppendBoundaryDiagnostic(ffprotocol::DiagnosticCode code, std::wstring volumeId,
                              const wchar_t* state) noexcept {
    try {
        ffprotocol::DiagnosticEvent event{};
        event.category = ffprotocol::DiagnosticCategory::IndexingError;
        event.code = code;
        event.volumeId = std::move(volumeId);
        event.state = state;
        ffprotocol::AppendDiagnostic(event);
    } catch (...) { }
}

bool SendVolumeList(HANDLE pipeHandle) {
    auto volumes = EnumerateFixedNtfsVolumes();

    std::vector<uint8_t> payload(sizeof(ffprotocol::VolumeListHeader) + volumes.size() * sizeof(ffprotocol::VolumeInfo));
    ffprotocol::VolumeListHeader header{static_cast<uint32_t>(volumes.size())};
    std::memcpy(payload.data(), &header, sizeof(header));
    if (!volumes.empty()) {
        std::memcpy(payload.data() + sizeof(header), volumes.data(), volumes.size() * sizeof(ffprotocol::VolumeInfo));
    }

    return WriteFrameWithDeadline(pipeHandle, static_cast<uint16_t>(MessageType::VolumeList), payload.data(), static_cast<uint32_t>(payload.size()));
}

// Returns std::nullopt (and does not reply -- the connection is closed by
// the caller) if the Handshake payload itself is malformed, or if version
// negotiation/authentication fails after having already sent the
// appropriate rejection reply.
std::optional<ClientIdentity> PerformHandshake(HANDLE pipeHandle, const std::wstring& installDir,
                                                bool& outShouldClose, ffprotocol::ProtocolVersion& outVersion) {
    auto frame = ffipc::ReadFrameOverlapped(pipeHandle, std::chrono::steady_clock::now() + kHandshakeTimeout);
    outShouldClose = true;
    if (!frame || frame->header.messageType != static_cast<uint16_t>(MessageType::Handshake)) {
        return std::nullopt; // first message must be Handshake
    }
    if (frame->payload.size() != sizeof(ffprotocol::HandshakeRequest)) {
        return std::nullopt;
    }

    ffprotocol::HandshakeRequest request{};
    std::memcpy(&request, frame->payload.data(), sizeof(request));

    if (!ffprotocol::IsVersionCompatible(ffprotocol::kCurrentProtocolVersion, request.clientVersion)) {
        SendIncompatibleVersion(pipeHandle);
        return std::nullopt;
    }

    // Opportunistic staleness check alongside every new connection (spec:
    // "checked ... opportunistically at handshake").
    CheckStalenessNow();

    ffprotocol::HandshakeRejectReason rejectReason{};
    auto identity = VerifyClientAtHandshake(pipeHandle, installDir, rejectReason);
    if (!identity) {
        SendHandshakeReject(pipeHandle, rejectReason);
        return std::nullopt;
    }

    outVersion = request.clientVersion.major == ffprotocol::kCurrentProtocolVersion.major
        ? ffprotocol::ProtocolVersion{ffprotocol::kCurrentProtocolVersion.major,
                                      (std::min)(request.clientVersion.minor, ffprotocol::kCurrentProtocolVersion.minor)}
        : request.clientVersion;
    ffprotocol::HandshakeAckPayload ack{outVersion};
    if (!WriteFrameWithDeadline(pipeHandle, static_cast<uint16_t>(MessageType::HandshakeAck), &ack, sizeof(ack))) {
        CloseClientIdentity(*identity);
        return std::nullopt;
    }

    outShouldClose = false;
    return identity;
}

} // namespace

void RunCtrlConnection(HANDLE pipeHandle, HANDLE shutdownEvent, const std::wstring& installDir,
                       ConnectionRegistry& registry) {
    const ConnectionRegistry::ConnectionId connectionId = static_cast<ConnectionRegistry::ConnectionId>(reinterpret_cast<uintptr_t>(pipeHandle));

    bool shouldClose = false;
    ffprotocol::ProtocolVersion negotiatedVersion{};
    auto identity = PerformHandshake(pipeHandle, installDir, shouldClose, negotiatedVersion);
    if (!identity) {
        CloseHandle(pipeHandle);
        return;
    }

    // Guards every WriteFrame call on pipeHandle from any thread -- this
    // loop plus any active scan/journal worker threads (VolumeScanner.h /
    // UsnJournalReader.h) all write to the same handle, and named-pipe
    // writes from multiple threads are not implicitly serialized.
    std::mutex writeMutex;
    std::vector<ActiveWorker> activeScans;
    std::vector<ActiveWorker> activeJournals;
    // A worker that observes a transport-fatal write failure sets this before
    // cancelling pending overlapped I/O. The control loop then cannot keep
    // dispatching on a byte stream whose frame boundary is no longer trusted.
    auto transportFailed = std::make_shared<std::atomic<bool>>(false);

    bool disconnect = false;
    auto idleDeadline = std::chrono::steady_clock::now() + kInboundIdleTimeout;
    // hardening-campaign Phase 7 H1: authorization validity is re-proven from
    // the SERVICE's monotonic clock -- initialized at connection establishment
    // (the handshake just verified the peer) so every connection generation
    // starts verified and counts its own attested span.
    auto lastRevalidated = std::chrono::steady_clock::now();
    // hardening-campaign Phase 7 H2: terminal exception boundary for the
    // connection generation loop. Any escaping exception (bad_alloc/
    // system_error from frame reads, payload copies, registry operations, or
    // thread spawn) becomes a diagnostic + defined disconnect instead of
    // std::terminate on this LocalSystem service. The ordered teardown BELOW
    // runs OUTSIDE the catch unconditionally, preserving the handle-staleness
    // invariant. The dispatch body keeps its original indentation so this
    // security fix stays reviewable as a boundary-only change.
    try {
    while (!disconnect && !transportFailed->load()) {
        auto frame = ReadActiveFrame(pipeHandle, shutdownEvent, idleDeadline);
        if (transportFailed->load()) {
            break;
        }
        if (!frame) {
            break; // I/O error, clean disconnect, or inbound idle timeout
        }
        // Any inbound frame resets the idle window: the engine heartbeats
        // every ~20s, so only a peer silent for the full kInboundIdleTimeout
        // is reaped.
        idleDeadline = std::chrono::steady_clock::now() + kInboundIdleTimeout;

        // Task 2.5 / CommandSurface.h: single, explicit allowlist gate. Any
        // message type outside the documented volume/journal/heartbeat
        // surface -- a reply-only type, a second Handshake, an unknown
        // value -- closes the connection before any work is performed.
        if (!IsAllowedClientRequest(frame->header.messageType)) {
            disconnect = true;
            break;
        }

        // hardening-campaign Phase 7 H1: wall-clock revalidation gate. The
        // allowlist gate above proved the frame is *allowed*; this proves the
        // peer is *still* the authorized client before ANY work runs -- on the
        // service's own cadence, so a client that never sends Heartbeat can no
        // longer extend the attested span (closes the client-owned-cadence
        // bypass; mirrors the engine's 20s verify cycle). Fail-closed: a
        // failed revalidation disconnects before dispatch.
        if (ShouldRevalidate(std::chrono::steady_clock::now(), lastRevalidated, kRevalidationInterval)) {
            if (!RevalidateClient(*identity, installDir)) {
                disconnect = true;
                break;
            }
            lastRevalidated = std::chrono::steady_clock::now();
        }

        switch (static_cast<MessageType>(frame->header.messageType)) {
            case MessageType::EnumerateVolumes: {
                std::lock_guard<std::mutex> lock(writeMutex);
                if (transportFailed->load()) {
                    disconnect = true;
                    break;
                }
                disconnect = !SendVolumeList(pipeHandle);
                break;
            }

            case MessageType::StartVolumeScan: {
                const bool useV1 = negotiatedVersion.major == 1;
                const size_t requestSize = useV1 ? sizeof(ffprotocol::StartVolumeScanRequestV1)
                                                 : sizeof(ffprotocol::StartVolumeScanRequest);
                if (frame->payload.size() < requestSize) {
                    disconnect = true;
                    break;
                }
                ffprotocol::StartVolumeScanRequest request{};
                if (useV1) {
                    ffprotocol::StartVolumeScanRequestV1 requestV1{};
                    std::memcpy(&requestV1, frame->payload.data(), sizeof(requestV1));
                    request.volumeId = requestV1.volumeId;
                    request.resumeCursorLengthBytes = requestV1.resumeCursorLengthBytes;
                    request.flags = 0;
                } else {
                    std::memcpy(&request, frame->payload.data(), sizeof(request));
                }
                const size_t expectedCursorBytes = frame->payload.size() - requestSize;
                if (!ffprotocol::IsScanCursorLengthValid(request.resumeCursorLengthBytes)
                    || !ffprotocol::AreStartVolumeScanFlagsValid(request.flags)
                    || expectedCursorBytes != request.resumeCursorLengthBytes) {
                    disconnect = true;
                    break;
                }
                std::vector<uint8_t> resumeCursor(
                    frame->payload.begin() + requestSize, frame->payload.end());

                const wchar_t driveLetter = ResolveVolumeIdToDriveLetter(request.volumeId);
                if (driveLetter == L'\0') {
                    disconnect = true;
                    break;
                }

                if (!registry.TryMarkVolumeScanStarted(connectionId, request.volumeId)) {
                    // Connection-scoped handles: a Start that would supersede
                    // another live connection's scan on this volume is rejected
                    // -- there is no dedicated rejection reply, so reject by
                    // closing the connection, consistent with an unauthorized
                    // Stop/Close.
                    disconnect = true;
                    break;
                }
                StopAndJoin(activeScans, request.volumeId.value); // supersede any prior scan of the same volume

                ActiveWorker worker;
                worker.volumeId = request.volumeId.value;
                worker.stopFlag = std::make_shared<std::atomic<bool>>(false);
                const auto volumeId = request.volumeId;
                const bool lowPriority = (request.flags & ffprotocol::kStartVolumeScanLowPriority) != 0;
                const auto stopFlag = worker.stopFlag;
                const auto peerVersion = negotiatedVersion;
                // hardening-campaign Phase 7 H2: register the slot BEFORE the
                // thread starts, so a throw between construction and insertion
                // can never orphan a writer holding the pipe handle
                // (StopAndJoinAll must always see every worker).
                activeScans.push_back(std::move(worker));
                activeScans.back().thread = std::thread([pipeHandle, &writeMutex, volumeId, driveLetter,
                                                          resumeCursor, lowPriority, peerVersion, stopFlag,
                                                          transportFailed] {
                    // hardening-campaign Phase 7 H2: the shared worker boundary
                    // makes any partial stream connection-fatal and never adds
                    // a terminal after an exception or failed frame write.
                    RunScanWorkerBoundary(pipeHandle, *transportFailed,
                        [&] {
                            return RunVolumeScan(pipeHandle, writeMutex, volumeId, driveLetter, resumeCursor,
                                                 lowPriority, peerVersion, *stopFlag, transportFailed.get());
                        },
                        [] {
                            WriteServiceLifecycleLog(
                                L"scan worker transport failure: connection abort requested; no terminal appended");
                        },
                        [volumeId](ScanWorkerExceptionKind kind) {
                            const bool unknown = kind == ScanWorkerExceptionKind::Unknown;
                            AppendBoundaryDiagnostic(ffprotocol::DiagnosticCode::FFDIAG_SCAN_FAILED,
                                std::to_wstring(volumeId.value), unknown
                                    ? L"scan-worker-unknown-exception" : L"scan-worker-exception");
                            WriteServiceLifecycleLog(unknown
                                ? L"scan worker unknown exception boundary: connection abort requested; no terminal appended"
                                : L"scan worker exception boundary: connection abort requested; no terminal appended");
                        });
                });
                break;
            }

            case MessageType::StopVolumeScan: {
                if (frame->payload.size() != sizeof(ffprotocol::StopVolumeScanRequest)) {
                    disconnect = true;
                    break;
                }
                ffprotocol::StopVolumeScanRequest request{};
                std::memcpy(&request, frame->payload.data(), sizeof(request));
                if (!registry.TryStopVolumeScan(connectionId, request.volumeId)) {
                    // Spec "Stop request from a different connection is
                    // rejected" -- the closed protocol has no dedicated
                    // rejection reply, so reject by closing the connection,
                    // consistent with how an unrecognized message type is
                    // handled.
                    disconnect = true;
                    break;
                }
                StopAndJoin(activeScans, request.volumeId.value);
                break;
            }

            case MessageType::OpenUsnJournal: {
                if (frame->payload.size() != sizeof(ffprotocol::OpenUsnJournalRequest)) {
                    disconnect = true;
                    break;
                }
                ffprotocol::OpenUsnJournalRequest request{};
                std::memcpy(&request, frame->payload.data(), sizeof(request));

                const wchar_t driveLetter = ResolveVolumeIdToDriveLetter(request.volumeId);
                if (driveLetter == L'\0') {
                    disconnect = true;
                    break;
                }

                if (!registry.TryMarkUsnJournalOpened(connectionId, request.volumeId)) {
                    // Connection-scoped handles: an Open that would supersede
                    // another live connection's journal on this volume is
                    // rejected -- there is no dedicated rejection reply, so
                    // reject by closing the connection, consistent with an
                    // unauthorized Stop/Close.
                    disconnect = true;
                    break;
                }
                StopAndJoin(activeJournals, request.volumeId.value);

                ActiveWorker worker;
                worker.volumeId = request.volumeId.value;
                worker.stopFlag = std::make_shared<std::atomic<bool>>(false);
                const auto volumeId = request.volumeId;
                const auto resumeUsn = request.resumeUsn;
                const auto stopFlag = worker.stopFlag;
                const auto peerVersion = negotiatedVersion;
                // hardening-campaign Phase 7 H2: register the slot BEFORE the
                // thread starts (same orphan-window fix as the scan worker).
                activeJournals.push_back(std::move(worker));
                activeJournals.back().thread = std::thread([pipeHandle, &writeMutex, volumeId, driveLetter,
                                                             resumeUsn, peerVersion, stopFlag, transportFailed] {
                    // hardening-campaign Phase 7 H2: terminal exception
                    // boundary for the journal worker. Journal failures are
                    // silent-Ended by design D6 -- an escaping exception maps
                    // to exactly that (worker ends, connection stays up, the
                    // engine reconciles) plus diagnostics; never terminate.
                    try {
                        const JournalStreamOutcome outcome =
                            RunUsnJournalStream(pipeHandle, writeMutex, volumeId, driveLetter, resumeUsn,
                                                peerVersion, *stopFlag, transportFailed.get());
                        if (transportFailed->load()) {
                            CancelIoEx(pipeHandle, nullptr);
                        }
                        if (outcome == JournalStreamOutcome::ResumePositionInvalid && peerVersion.major != 1) {
                            // D6/task 7.6: tell the engine its ResumeUsn aged
                            // out of the journal's retained range before this
                            // worker tears down, so it falls back to a
                            // reconciliation sweep instead of a blind resume.
                            // Major-1 peers predate this additive message and
                            // must not receive an unknown reply-only type.
                            ffprotocol::JournalResumeInvalidPayload payload{volumeId};
                            std::lock_guard<std::mutex> lock(writeMutex);
                            WriteFrameWithDeadline(pipeHandle,
                                                   static_cast<uint16_t>(MessageType::JournalResumeInvalid),
                                                   &payload, sizeof(payload), transportFailed.get());
                        }
                    } catch (const std::exception&) {
                        try {
                            AppendBoundaryDiagnostic(ffprotocol::DiagnosticCode::FFDIAG_UNKNOWN, {},
                                                     L"journal-worker-exception");
                            WriteServiceLifecycleLog(
                                L"journal worker exception boundary: journal stream ends; engine reconciles");
                        } catch (...) { }
                    } catch (...) {
                        try {
                            AppendBoundaryDiagnostic(ffprotocol::DiagnosticCode::FFDIAG_UNKNOWN, {},
                                                     L"journal-worker-unknown-exception");
                            WriteServiceLifecycleLog(
                                L"journal worker unknown exception boundary: journal stream ends; engine reconciles");
                        } catch (...) { }
                    }
                });
                break;
            }

            case MessageType::CloseUsnJournal: {
                if (frame->payload.size() != sizeof(ffprotocol::CloseUsnJournalRequest)) {
                    disconnect = true;
                    break;
                }
                ffprotocol::CloseUsnJournalRequest request{};
                std::memcpy(&request, frame->payload.data(), sizeof(request));
                if (!registry.TryCloseUsnJournal(connectionId, request.volumeId)) {
                    disconnect = true;
                    break;
                }
                StopAndJoin(activeJournals, request.volumeId.value);
                break;
            }

            case MessageType::Heartbeat: {
                // hardening-campaign Phase 7 H1: the wall-clock gate above
                // owns time-based revalidation; Heartbeat keeps its
                // event-driven re-proof (task 3.5 -- revoked group membership
                // or a replaced/unsigned binary on a long-lived connection,
                // not only at initial Handshake) unless the gate just ran on
                // this very same frame, in which case the timestamp is fresh
                // and only the ack is due.
                if (ShouldRevalidate(std::chrono::steady_clock::now(), lastRevalidated,
                                     kRevalidationFreshEpsilon)) {
                    if (!RevalidateClient(*identity, installDir)) {
                        disconnect = true;
                        break;
                    }
                    lastRevalidated = std::chrono::steady_clock::now();
                }
                std::lock_guard<std::mutex> lock(writeMutex);
                if (transportFailed->load()) {
                    disconnect = true;
                    break;
                }
                disconnect = !WriteFrameWithDeadline(pipeHandle, static_cast<uint16_t>(MessageType::HeartbeatAck), transportFailed.get());
                break;
            }

            default:
                // Safety net: unreachable for any type IsAllowedClientRequest
                // accepts today (every allowed type has an explicit case
                // above). If a future maintainer widens the allowlist without
                // adding a handler here, the connection closes instead of
                // silently ignoring the request.
                disconnect = true;
                break;
        }
    }
    } catch (const std::exception&) {
        // hardening-campaign Phase 7 H2: translate to a defined connection
        // error -- diagnostic + lifecycle log, then the ordered teardown
        // below (new transport + re-authentication is the engine's half of
        // the recovery: broken pipe -> backoff reconnect -> full re-gate).
        try {
            AppendBoundaryDiagnostic(ffprotocol::DiagnosticCode::FFDIAG_UNKNOWN, {},
                                     L"ctrl-dispatch-exception");
            WriteServiceLifecycleLog(L"ctrl connection exception boundary: disconnecting for teardown");
        } catch (...) { }
        disconnect = true;
    } catch (...) {
        try {
            AppendBoundaryDiagnostic(ffprotocol::DiagnosticCode::FFDIAG_UNKNOWN, {},
                                     L"ctrl-dispatch-unknown-exception");
            WriteServiceLifecycleLog(L"ctrl connection unknown exception boundary: disconnecting for teardown");
        } catch (...) { }
        disconnect = true;
    }

    StopAndJoinAll(activeScans);
    StopAndJoinAll(activeJournals);
    registry.TeardownConnection(connectionId);
    CloseClientIdentity(*identity);
    CloseHandle(pipeHandle);
}

} // namespace ffindexsvc
