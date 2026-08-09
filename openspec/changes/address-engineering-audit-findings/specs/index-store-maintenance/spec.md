## ADDED Requirements

### Requirement: Periodic ANALYZE
After every bulk insert batch of 10,000 records, the engine SHALL execute ANALYZE on the SQLite store to update query planner statistics.

#### Scenario: Bulk insert triggers ANALYZE
- **WHEN** the engine ingests 10,000 MFT records into the SQLite store
- **THEN** ANALYZE is automatically executed before the next query batch

### Requirement: Periodic VACUUM
During engine idle time, the engine SHALL evaluate whether VACUUM is needed. VACUUM SHALL run only if the database file grew by more than 10% since the last VACUUM and the user has been idle for at least 5 minutes.

#### Scenario: VACUUM gated by size and idle
- **WHEN** the engine is idle, the DB grew 15% since last VACUUM, and no UI input occurred for 5 minutes
- **THEN** VACUUM is executed and the last-VACUUM timestamp is updated

#### Scenario: VACUUM skipped when not needed
- **WHEN** the engine is idle but the DB grew only 2% since last VACUUM
- **THEN** VACUUM is skipped and no I/O spike occurs
