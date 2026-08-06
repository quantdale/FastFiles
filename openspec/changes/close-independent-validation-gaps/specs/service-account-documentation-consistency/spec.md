## ADDED Requirements

### Requirement: Service-Account Documentation Consistency
All repository documentation and guidance that describes the `FastFilesIndexSvc` privileged service account SHALL consistently state the LocalSystem constrained-broker model: the service runs as LocalSystem as a deliberately constrained privileged broker; raw MFT/USN scanning is implemented; the degraded path is the active and safe production path while the Authenticode signature pins and privileged-path operational validation remain incomplete; and the LocalSystem blast radius is a deliberate, documented decision that SHALL NOT be minimized or described inaccurately (for example, as a minimal-privilege virtual account).

#### Scenario: Guidance matches the authoritative model
- **WHEN** any repository guidance file (for example, `AGENTS.md`, `CLAUDE.md`, `CODE_INDEX.md`, `README.md`) describes the privileged service account
- **THEN** the description SHALL state that `FastFilesIndexSvc` runs as LocalSystem as the constrained privileged broker, SHALL state that raw MFT/USN scanning is implemented, and SHALL state that the degraded path is the active and safe production path while pins and privileged-path validation are incomplete

#### Scenario: Blast radius is not minimized
- **WHEN** a repository document describes the impact of running as LocalSystem
- **THEN** the document SHALL acknowledge the larger blast radius of a LocalSystem compromise and SHALL NOT characterize the service as a minimal-privilege virtual account or otherwise understate that impact

#### Scenario: Superseded historical statements are explicitly marked
- **WHEN** repository documentation retains a statement from before the LocalSystem decision (for example, a virtual-account-only model)
- **THEN** the retained statement SHALL be explicitly marked as superseded or historical and SHALL point to the LocalSystem constrained-broker decision evidence, and SHALL NOT be presented as current

#### Scenario: Repository-wide documentation search is clean
- **WHEN** a repository-wide search is run for a superseded account descriptor such as "virtual service account", "never LocalSystem", "stubbed" (as a current state rather than the known-pending privileged path), or "SeBackupPrivilege only"
- **THEN** the search SHALL return only explicitly-marked superseded/historical text and SHALL NOT return current guidance asserting the superseded model
