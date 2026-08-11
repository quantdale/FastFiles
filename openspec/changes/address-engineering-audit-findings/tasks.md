## 1. Authenticated Engine Launch (security-hardening, authenticated-engine-launch)

- [ ] 1.1 Add build-time pin injection mechanism (CI environment variable or signed resource) for release builds
- [ ] 1.2 Replace all-zero placeholder pins in PinnedSignatures.h with build-time-injected values or dev-mode fallback
- [ ] 1.3 Add startup self-check that fails with clear error if pins are still placeholders in a non-debug build
- [ ] 1.4 Add Authenticode signature verification to EngineDiscovery.cpp before launching any engine executable
- [ ] 1.5 Add canonical path validation (GetFinalPathNameByHandle) to reject symlinks/junctions in the install directory scan
- [ ] 1.6 Log launched path and signature thumbprint for auditability
- [ ] 1.7 Add unit test for signature verification failure path

## 2. IPC Reliability (ipc-reliability)

- [ ] 2.1 Replace PeekNamedPipe + Sleep(2) busy-wait in ServiceConnection.cpp with ReadFile overlapped I/O
- [ ] 2.2 Add GetOverlappedResult with timeout fallback for non-overlapped test scenarios
- [ ] 2.3 Add unit test for async pipe read completion and timeout paths

## 3. Data Structure Safety (data-structure-safety)

- [x] 3.1 Fix FlatHashMap::operator[] grow trigger to fire at 75% load factor
- [x] 3.2 Add maximum probe distance (64 steps) safety break to FlatHashMap insertion loop
- [x] 3.3 Add unit test that fills FlatHashMap to 100% capacity and verifies insertion still works (triggers grow)
- [x] 3.4 Add unit test for max probe distance abort path

## 4. Snapshot Versioning (snapshot-versioning)

- [x] 4.1 Add format version field to SnapshotDirectory header in SnapshotFormat.h
- [x] 4.2 Update SnapshotPublisher to write version on every publication
- [x] 4.3 Update UI mapping code to read version and reject incompatible snapshots with re-map
- [x] 4.4 Add unit test for version mismatch detection and re-map path

## 5. Index Store Maintenance (index-store-maintenance)

- [ ] 5.1 Add ANALYZE trigger after every 10,000 bulk insert records in IndexPipeline.cpp
- [ ] 5.2 Add idle-time VACUUM scheduler in Store.cpp gated by 24-hour interval and 10% size growth threshold
- [ ] 5.3 Add configuration options for VACUUM interval and size threshold in Settings.h
- [ ] 5.4 Add unit test for VACUUM gating logic

## 6. Projection Eviction (projection-eviction)

- [ ] 6.1 Add configurable maximum projection size setting (default 500 MB) in Projection.h
- [ ] 6.2 Implement LRU eviction policy in Projection.cpp when size limit is exceeded
- [ [ ] 6.3 Add SQLite reload path for evicted directory entries
- [ ] 6.4 Add unit test for projection size limit and eviction order
- [ ] 6.5 Add memory usage monitoring and warning when projection approaches limit

## 7. Structured Diagnostics (structured-diagnostics)

- [ ] 7.1 Define diagnostic error code enum in Diagnostics.h
- [ ] 7.2 Update all diagnostic emission sites to include structured code alongside message
- [ ] 7.3 Add JSON serialization option for diagnostic stream
- [ ] 7.4 Add unit test for structured diagnostic output format

## 8. Verification and CI

- [ ] 8.1 Add CI check that fails if TEMP-DEBUG or DEBUG_STALL patterns are found in non-test code
- [x] 8.2 Add CTest test for FlatHashMap 100% occupancy
- [ ] 8.3 Add CTest test for IPC handshake over real named pipe
- [ ] 8.4 Add CTest test for SnapshotPublisher concurrency under thread contention
- [x] 8.5 Verify entire test suite passes with /W4 /WX on Windows toolchain
