## ADDED Requirements

### Requirement: Appearance density selection setting
The Settings General/appearance surface SHALL offer Compact and Comfortable density. The choice SHALL persist in the general settings file under `appearance.density` as `Compact` or `Comfortable`. A missing field SHALL load Compact. Changing density SHALL apply through the same ApplyTheme-style fan-out used for theme changes and SHALL NOT require an application restart.

#### Scenario: Density round-trips
- **WHEN** the user selects Comfortable and saves settings
- **THEN** a subsequent load returns Comfortable and browse/search/sidebar row metrics use the Comfortable density profile

#### Scenario: Missing density defaults Compact
- **WHEN** an existing settings file has an appearance section with theme but no density field
- **THEN** density SHALL be Compact
