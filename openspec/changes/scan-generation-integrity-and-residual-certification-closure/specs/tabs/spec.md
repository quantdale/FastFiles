## ADDED Requirements

### Requirement: Narrow overflow keeps logical tabs reachable

When the tab strip cannot show every open tab at the readable minimum width, the system SHALL keep the full logical set of tabs, SHALL show an overflow affordance, and SHALL keep every hidden tab reachable from that affordance. Choosing a hidden tab SHALL activate that tab's workspace. The active tab SHALL remain represented either in the visible strip or as the current overflow selection. Closing or switching tabs SHALL NOT assign one tab's identity to a different workspace. Keyboard activation SHALL be able to open the overflow affordance and choose a hidden tab. The many-tabs visual scenario SHALL prove the logical count, the visible count, and overflow reachability separately. Its logical-count floor SHALL remain at least 8 and SHALL NOT be lowered to 4. The scenario SHALL NOT pass by counting only visible tab buttons or by treating a hidden tab as deleted.

#### Scenario: Eight logical tabs at the narrow width

- **WHEN** the many-tabs scenario opens tabs at the scenario's narrow width through the same new-tab action that historically expected 8
- **THEN** the logical tab count SHALL be at least 8
- **AND** the visible tab-button count SHALL be reported separately from the logical count
- **AND** an overflow affordance SHALL be present whenever one or more of those tabs is hidden

#### Scenario: A hidden tab activates its own workspace

- **WHEN** a tab is hidden because it does not fit and the user chooses it from the overflow affordance, including by keyboard
- **THEN** that tab's workspace SHALL become the active workspace
- **AND** the activated identity SHALL be the tab that was chosen

#### Scenario: Normal width shows every open tab

- **WHEN** the same open tabs are shown at a width where every tab fits in the strip
- **THEN** each open tab SHALL have a visible tab button
- **AND** closing or switching one tab SHALL leave the other tabs' workspaces and identities unchanged

#### Scenario: Product and harness are scored separately

- **WHEN** the narrow many-tabs scenario is evaluated
- **THEN** the result SHALL record the logical tab count, the visible tab-button count, and whether a hidden tab can be activated
- **AND** a visible count below 8 SHALL NOT alone be reported as deleted tabs when the logical count is 8 and hidden tabs remain reachable
- **AND** a logical count below 8 SHALL be a product failure even if the visible-button expectation was reduced
