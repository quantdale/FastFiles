# file-type-categorization Specification

## Purpose
Extension-to-category mapping with shipped defaults, user-editable mappings, an Other/Uncategorized fallback, case-insensitive most-specific matching, and category filtering of analysis views.

## Requirements
### Requirement: Default Extension-to-Category Mapping
The system SHALL ship with a default extension-to-category mapping covering common categories, including video, image, document, archive, executable, development files, virtual machine images, and games, so breakdown-by-category views function without any user configuration.

#### Scenario: Default categorization works out of the box
- **WHEN** a user opens a breakdown-by-category view without having customized the mapping
- **THEN** the system SHALL classify files into the shipped default categories based on their file extensions

### Requirement: User-Editable Category Mapping
The extension-to-category mapping SHALL be configurable and editable by the user — adding or removing extensions, reassigning an extension to a different category, or adding new categories — without requiring a software update or code change.

#### Scenario: Editing the mapping takes effect immediately
- **WHEN** a user reassigns an extension to a different category, or adds a new category or extension mapping
- **THEN** the system SHALL apply the updated mapping to subsequent breakdown-by-category views without requiring a new build or code change

#### Scenario: Customized mapping persists across restarts
- **WHEN** a user has customized the mapping and restarts the application
- **THEN** the customized mapping SHALL persist and SHALL continue to be used for categorization

### Requirement: Unmatched Extension Fallback Category
A file whose extension does not match any entry in the configured mapping, or that has no extension, SHALL be classified into an explicit "Other/Uncategorized" category rather than being omitted from breakdown totals.

#### Scenario: Extension has no configured mapping
- **WHEN** a file's extension does not match any entry in the extension-to-category mapping
- **THEN** the system SHALL classify that file under an explicit "Other/Uncategorized" category, and that category SHALL be included in breakdown-by-category totals

### Requirement: Category Breakdown View
The system SHALL provide a breakdown-by-category view showing, for a selected volume or folder scope, the aggregate size and item count contributed by each category present in that scope.

#### Scenario: Viewing category breakdown for a scope
- **WHEN** a user opens the breakdown-by-category view for a volume or folder scope
- **THEN** the system SHALL display each category present in that scope along with its aggregate size and item count

### Requirement: Case-Insensitive, Most-Specific Extension Matching
Extension matching against the mapping SHALL be case-insensitive, and when more than one configured pattern could match a given file, the most specific matching pattern SHALL be applied.

#### Scenario: Matching ignores case
- **WHEN** a file has an uppercase or mixed-case extension, such as ".MP4"
- **THEN** the system SHALL match it identically to its lowercase equivalent according to the configured mapping

#### Scenario: Conflicting pattern specificity resolves to the most specific match
- **WHEN** more than one configured pattern in the mapping could match a given file's extension
- **THEN** the system SHALL apply the most specific matching pattern rather than an arbitrary or first-registered one

### Requirement: Category Data Consumed by Storage Drill-Down and Treemap Views
The extension-to-category mapping and its resulting classification SHALL be available as a filter within `storage-drilldown-and-treemap` views, scoping displayed items to a selected category.

#### Scenario: Filtering a drill-down or treemap view by category
- **WHEN** a user selects a category filter within a drill-down or treemap view
- **THEN** the system SHALL scope the displayed items to only those classified under the selected category, and SHALL recalculate displayed sizes and percentages relative to that filtered scope
