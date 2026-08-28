## ADDED Requirements

### Requirement: Density profile is part of appearance
The published `gUiDensityProfile` SHALL follow the persisted appearance density. Theme changes and density changes MAY occur independently. High Contrast SHALL continue to suppress token overlays regardless of density.

#### Scenario: Theme and density compose
- **WHEN** the user is in Dark theme and Comfortable density
- **THEN** surfaces use the dark token set and Comfortable metrics together
