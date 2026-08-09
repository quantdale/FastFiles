## ADDED Requirements

### Requirement: Async pipe reads
The engine SHALL use overlapped ReadFile with GetOverlappedResult for reading from the service named pipe. The pipe handle SHALL be opened with FILE_FLAG_OVERLAPPED.

#### Scenario: Async read completes
- **WHEN** the service writes a batch of scan records to the pipe
- **THEN** the engine overlapped read completes and the batch is parsed without polling

#### Scenario: Async read timeout
- **WHEN** no data is available on the pipe within the configured timeout
- **THEN** GetOverlappedResult returns WAIT_TIMEOUT and the engine retries or remains idle
