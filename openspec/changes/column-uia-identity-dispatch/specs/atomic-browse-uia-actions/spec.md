## ADDED Requirements
### Requirement: Stable identity at action execution
Column View accessibility actions SHALL preserve the provider's original column and item identity through owner-thread dispatch. Identity validation SHALL share the model lock with mutation or activation-target capture. Hidden, retired or replaced elements SHALL return element-not-available without acting on a replacement.

#### Scenario: Directory refresh replaces a resolved row
- **WHEN** a directory refresh replaces a row after an accessibility provider resolves it but before its action executes
- **THEN** selection, membership changes, focus, invocation and scroll actions reject the original provider without changing the replacement

#### Scenario: Membership changes before dispatch
- **WHEN** AddToSelection or RemoveFromSelection executes for the same original item
- **THEN** current membership is evaluated atomically and repeated operations remain idempotent

#### Scenario: Fragment retires before dispatch
- **WHEN** the browse workspace is hidden or its owner or fragment lifetime retires
- **THEN** the queued action fails without invoking navigation or mutating selection
