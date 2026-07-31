#pragma once

namespace ffindexsvc {

// Captures a hash of the on-disk service binary at the path this process
// was loaded from. Must be called once, early in startup, before the
// on-disk file could plausibly have been replaced by an update.
void CaptureLoadedBinaryHash();

// Starts a background timer that periodically re-hashes the on-disk
// binary and compares it against the hash captured by
// CaptureLoadedBinaryHash (task 3.9). On mismatch, terminates the process
// abnormally so SCM's configured failure actions (task 3.10) restart it
// with the new binary -- no external SERVICE_STOP call is involved (spec
// "Self-Directed Staleness Recovery").
void StartStalenessMonitor();

// Stops the background timer (called from the service control handler on
// a clean SERVICE_CONTROL_STOP).
void StopStalenessMonitor();

// Performs the same on-disk-vs-loaded hash check immediately, e.g.
// opportunistically at Handshake (spec: "checked ... opportunistically at
// handshake"). Terminates the process abnormally on mismatch; otherwise
// returns normally.
void CheckStalenessNow();

} // namespace ffindexsvc
