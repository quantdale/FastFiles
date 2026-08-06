# search-query-engine Specification

## Purpose
The search language and execution engine: tokenization with implicit AND, structured filters and range operators, wildcards, registry-based filter keys, in-memory projection matching, scoped execution, relevance ranking and explicit sort orders, path reconstruction, and Unicode-correct matching.

## Requirements
### Requirement: Query Tokenization with Implicit AND
The query engine SHALL split an input query string into tokens on unquoted whitespace, SHALL classify each token as a structured filter, a wildcard pattern, or a free-text term, and SHALL require every token to match (implicit AND) for an entry to be included in results.

#### Scenario: Multiple free-text terms all narrow the result set
- **WHEN** a user enters the query `report final`
- **THEN** the engine SHALL return only entries whose filename (or, per scope, path) contains both `report` and `final`

#### Scenario: A single term matches broadly
- **WHEN** a user enters the single-term query `budget`
- **THEN** the engine SHALL return every entry within the active scope whose filename contains `budget`, case-insensitively

### Requirement: Structured Filter Parsing
The query engine SHALL recognize `key:value` tokens for the registered filter keys `ext`, `size`, `modified`, `kind`, `name`, and `folder`, and SHALL support double-quoted values so a filter value may contain spaces or other characters that would otherwise be treated as token boundaries.

#### Scenario: Extension filter narrows by file extension
- **WHEN** a user enters `ext:pdf`
- **THEN** the engine SHALL return only entries whose file extension matches `pdf` (case-insensitively, without requiring the user to include the leading dot)

#### Scenario: Quoted value preserves embedded spaces
- **WHEN** a user enters `folder:"Program Files"`
- **THEN** the engine SHALL treat `Program Files` as a single filter value and SHALL NOT split it into two separate tokens

#### Scenario: Combining a structured filter with free text
- **WHEN** a user enters `ext:docx quarterly`
- **THEN** the engine SHALL return only entries with a `.docx` extension whose filename also contains `quarterly`

### Requirement: Comparison and Range Operators for Numeric and Date Filters
The `size:` filter SHALL support the comparison operators `>`, `<`, `>=`, `<=`, `=` with unit suffixes (`B`, `KB`, `MB`, `GB`) and an explicit `..`-delimited range form. The `modified:` filter SHALL support absolute dates, `..`-delimited date ranges, and relative offsets (e.g., a number of days).

#### Scenario: Greater-than size comparison
- **WHEN** a user enters `size:>10MB`
- **THEN** the engine SHALL return only entries whose size in bytes exceeds 10 * 1024 * 1024

#### Scenario: Size range
- **WHEN** a user enters `size:10MB..100MB`
- **THEN** the engine SHALL return only entries whose size falls within that inclusive range

#### Scenario: Relative modified-date filter
- **WHEN** a user enters `modified:>7d`
- **THEN** the engine SHALL return only entries whose last-modified timestamp is more recent than 7 days before the current time

### Requirement: Wildcard Pattern Matching
A bare token containing `*` or `?` outside a recognized filter key SHALL be compiled as a glob pattern and matched against the filename, where `*` matches zero or more characters and `?` matches exactly one character.

#### Scenario: Extension wildcard
- **WHEN** a user enters `*.pdf`
- **THEN** the engine SHALL return every entry whose filename ends in `.pdf`

#### Scenario: Wildcard with a single-character placeholder
- **WHEN** a user enters `report_??.docx`
- **THEN** the engine SHALL return entries whose filename matches that pattern with exactly two characters in the `??` position

### Requirement: Extensible Filter Registry
Filter keys SHALL be implemented as entries in a filter registry, each providing a value parser and a predicate factory evaluated against the in-memory projection, such that adding a new filter key SHALL NOT require modifying the tokenizer, the AND-combination logic, or any other filter's implementation.

#### Scenario: Adding a new filter key does not alter existing filter behavior
- **WHEN** a new filter key is registered (for example, in a later change)
- **THEN** all previously registered filter keys SHALL continue to parse and match exactly as before, with no changes to their registry entries required

#### Scenario: kind: filter is registry-based with a minimal default category set
- **WHEN** a user enters `kind:image`
- **THEN** the engine SHALL return entries matching a built-in default `image` category (evaluated via the same registry mechanism as any other filter key), without requiring the query parser itself to know the full set of categories

### Requirement: Graceful Handling of Unrecognized Filter Keys
A token matching the `key:value` shape with a key not present in the filter registry SHALL NOT cause a parse failure or be silently discarded; it SHALL be matched as a literal free-text substring against the filename, and the query engine SHALL report the key as unrecognized so the caller can surface an indication to the user.

#### Scenario: Unrecognized key falls back to literal text matching
- **WHEN** a user enters `owner:alice`
- **THEN** if `owner` is not a registered filter key, the engine SHALL match entries containing the literal substring `owner:alice` (or an equivalent literal fallback) rather than returning zero results with no explanation, and SHALL report that `owner` was not a recognized filter key

### Requirement: Partial and Substring Filename Matching
The query engine SHALL match free-text terms as case-insensitive substrings anywhere within a candidate's filename, executed against the same in-memory projection maintained for the filesystem index, without issuing a database query per search execution.

#### Scenario: Substring match in the middle of a filename
- **WHEN** a user enters the term `invoice`
- **THEN** the engine SHALL match a file named `2024_invoice_final.xlsx` even though `invoice` is neither a prefix nor a suffix of the filename

#### Scenario: Matching reads the in-memory projection directly
- **WHEN** a search is executed while `FastFilesEngine`'s current snapshot generation is mapped into the searching process
- **THEN** the match SHALL be computed by scanning the mapped in-memory projection directly, with no SQL query issued and no additional IPC round trip to `FastFilesEngine` for the scan itself

### Requirement: Scoped Query Execution
The query engine SHALL accept an explicit scope parameter — Current Folder, Current Folder and Subfolders, Current Drive, or All Indexed Locations — and SHALL restrict the candidate set to entries within that scope before or while matching, regardless of how a caller presents scope selection.

#### Scenario: Current Folder scope excludes subfolder contents
- **WHEN** a query is executed with scope Current Folder for a given directory
- **THEN** the engine SHALL only consider entries directly contained in that directory, and SHALL NOT match entries located in its subfolders

#### Scenario: Current Folder and Subfolders scope includes nested entries
- **WHEN** a query is executed with scope Current Folder and Subfolders for a given directory
- **THEN** the engine SHALL consider entries at any depth under that directory

#### Scenario: All Indexed Locations scope spans every indexed volume
- **WHEN** a query is executed with scope All Indexed Locations
- **THEN** the engine SHALL consider entries across every volume currently covered by the in-memory projection, not only the volume containing the current directory

### Requirement: Default Relevance Ranking
When sorted by relevance, results SHALL be ordered by match tier — exact filename match, then prefix match, then substring-elsewhere match, then path-only match — with ties broken first by shorter filename, then by shorter full path, then by ordinal alphabetical order.

#### Scenario: Exact match outranks a prefix match
- **WHEN** a query for `report` matches both a file named exactly `report` (before its extension) and a file named `report_final`
- **THEN** the entry with the exact filename match SHALL be ranked above the prefix match

#### Scenario: Prefix match outranks a mid-name substring match
- **WHEN** a query for `report` matches both `report_final.docx` and `annual_report.docx`
- **THEN** `report_final.docx` (prefix match) SHALL be ranked above `annual_report.docx` (substring-only match)

#### Scenario: Shorter filename breaks a tie within the same match tier
- **WHEN** two entries are both prefix matches for the same query term
- **THEN** the entry with the shorter filename SHALL be ranked above the one with the longer filename

### Requirement: Explicit Sort Orders
In addition to relevance, the query engine SHALL support sorting the result set by filename, full path, size, last-modified date, and creation date, in either ascending or descending direction, with a deterministic secondary alphabetical tiebreak when the primary sort key is equal across entries.

#### Scenario: Sorting by size descending
- **WHEN** a caller requests the current result set sorted by size, descending
- **THEN** the engine SHALL return entries ordered from largest to smallest size, with entries of equal size ordered alphabetically by filename

#### Scenario: Sorting by modified date
- **WHEN** a caller requests the current result set sorted by last-modified date, descending
- **THEN** the engine SHALL return the most recently modified entries first

### Requirement: Path Hierarchy Reconstruction from a Matched Entry
Given a matched entry, the query engine SHALL be able to reconstruct the ordered list of path segments from the volume root down to that entry by walking the in-memory projection's parent-directory-ID chain, without relying on any stored full-path string.

#### Scenario: Reconstructing a deeply nested entry's path
- **WHEN** a matched file's parent-directory-ID chain is walked from the entry up to the volume-root sentinel
- **THEN** the engine SHALL return the ordered, root-to-entry list of directory names (and the final entry's own name) reflecting the current in-memory projection

#### Scenario: Reconstruction fails gracefully on a broken chain
- **WHEN** a directory in the parent-ID chain can no longer be resolved (for example, it was deleted after the match was produced)
- **THEN** the engine SHALL return the segments it could resolve up to that point along with an explicit indication of where resolution stopped, rather than raising an unhandled error

### Requirement: Unicode and Special-Character Matching Correctness
Matching SHALL operate on UTF-16 filenames using ordinal (non-locale-sensitive) case folding, SHALL correctly compare filenames containing surrogate pairs, and SHALL NOT apply Unicode normalization (NFC/NFD) or locale-sensitive collation when comparing or matching.

#### Scenario: Case-insensitive match is locale-independent
- **WHEN** the same query and the same filenames are matched under two different Windows display-language/locale settings
- **THEN** the match results SHALL be identical in both cases

#### Scenario: Filenames containing characters outside the Basic Multilingual Plane match correctly
- **WHEN** a filename contains a character represented as a UTF-16 surrogate pair
- **THEN** a query term matching that character's grapheme SHALL correctly match the filename, and no match boundary computed for that filename SHALL split the surrogate pair

#### Scenario: Special characters in free-text terms match literally
- **WHEN** a user enters a free-text term containing spaces, parentheses, apostrophes, or accented characters (for example, `client's report (v2).docx` as a quoted `name:` value)
- **THEN** the engine SHALL match the literal characters as given, without requiring escaping beyond the query grammar's own quoting rule
