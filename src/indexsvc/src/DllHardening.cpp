#include "DllHardening.h"

#include <windows.h>

namespace ffindexsvc {

void HardenDllSearchPath() noexcept {
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
}

} // namespace ffindexsvc
