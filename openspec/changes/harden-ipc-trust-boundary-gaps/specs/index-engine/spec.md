## ADDED Requirements

### Requirement: Periodic Service-Peer Revalidation On An Active Connection
While the privileged connection is `Active`, `FastFilesEngine` SHALL re-verify the connected service process image path (under the ACL-protected install directory) and pinned Authenticode signature on a periodic bound no looser than the existing heartbeat cadence. Failure SHALL be treated as a disconnect: the engine transitions to `Disconnected` and remains in degraded mode until a later successful handshake. This is independent of whether Authenticode pins are production-provisioned; placeholder pins continue to fail closed.

#### Scenario: Heartbeat-cadence revalidation failure drops to degraded
- **WHEN** an Active connection's periodic service-peer check fails (wrong image path or signature)
- **THEN** the engine SHALL leave Active, SHALL NOT keep using that pipe, and directory listing SHALL continue via degraded mode

#### Scenario: Successful revalidation keeps the connection
- **WHEN** the periodic check still matches the installed, pinned `FastFilesIndexSvc.exe`
- **THEN** the engine SHALL remain Active and SHALL NOT bounce the connection
