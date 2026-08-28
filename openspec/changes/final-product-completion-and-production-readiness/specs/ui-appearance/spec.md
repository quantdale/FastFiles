## ADDED Requirements

### Requirement: One shell visual hierarchy
Chrome, sidebar, Home, Column View, Search, Storage Analysis, details/preview, settings, and owned dialogs SHALL consume `GetUiTheme()` / `UiMetrics` / `GetUiDensity()` and the shared component painters in `UiComponents.h`. New UI code SHALL NOT introduce file-local RGB literals except High Contrast `GetSysColor` fallbacks.

#### Scenario: Density changes reach browse rows
- **WHEN** the active density profile is Comfortable
- **THEN** Column View file rows use the Comfortable row height from `GetUiDensity()`

#### Scenario: High Contrast still suppresses overlays
- **WHEN** Windows High Contrast is active
- **THEN** chrome, sidebar, search, and storage continue to suppress token hover/press/selection-soft overlays
