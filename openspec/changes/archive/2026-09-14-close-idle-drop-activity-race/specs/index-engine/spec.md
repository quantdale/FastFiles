## ADDED Requirements

### Requirement: Idle Drop Reconnects On Activity Even When Activity Races The Drop
After a configurable idle period with no significant activity, `FastFilesEngine` MAY drop its privileged connection (foundation "Standing exposure window"). The next UI request or filesystem-change activity burst SHALL request reconnection. If activity arrives while the idle callback is still running (including during store maintenance / VACUUM that precedes the drop), FastFiles SHALL NOT leave the privileged connection dropped with no subsequent reconnect: either the drop is cancelled in favor of the reconnect, or the drop completes and the activity-driven reconnect still runs.

#### Scenario: Activity during the idle callback
- **WHEN** the idle timer has fired, the idle callback is in progress (for example running store maintenance), and a UI request or directory-change notification arrives before the callback returns
- **THEN** the engine SHALL be Active (or reconnecting toward Active) after the race, not stuck Disconnected/IdleDropped until the next idle timeout

#### Scenario: Quiet idle still drops
- **WHEN** no activity arrives for the configured idle period and none arrives during the idle callback
- **THEN** the engine SHALL drop the privileged connection and SHALL reconnect on the next later activity burst

#### Scenario: Activity after a completed idle drop
- **WHEN** an idle drop has fully completed and a UI request then arrives
- **THEN** the engine SHALL request reconnection of the privileged path
