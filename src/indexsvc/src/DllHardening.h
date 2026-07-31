#pragma once

namespace ffindexsvc {

// Must be called before any LoadLibrary call in the process (task 3.3;
// design.md D4 DLL/binary hardening) -- this is the exact mitigation for
// the DLL-search-order LPE class Everything itself shipped (CVE-2020-24567).
// All of this build's own dependencies are statically linked or referenced
// by fully-qualified path, so this call is defense-in-depth against any
// dependency (including ones pulled in transitively by the CRT or a future
// dependency) resolving an unqualified DLL name from an unsafe directory.
void HardenDllSearchPath() noexcept;

} // namespace ffindexsvc
