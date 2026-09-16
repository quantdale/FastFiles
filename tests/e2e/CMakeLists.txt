# The live E2E runner needs an interactive Windows desktop and is intentionally
# not registered with the ordinary CTest suite.  Keep it visible as an explicit
# IDE target so the expensive black-box gate is discoverable without becoming a
# default edit/build dependency.
include("${PROJECT_SOURCE_DIR}/cmake/FastFilesPowerShell.cmake")
ff_resolve_powershell(FF_PWSH_EXECUTABLE)

if(FF_PWSH_EXECUTABLE)
  add_custom_target(fastfiles_e2e
    COMMAND "${FF_PWSH_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass -File
            "${PROJECT_SOURCE_DIR}/verify/e2e/Run-FastFilesE2E.ps1"
            -RepoRoot "${PROJECT_SOURCE_DIR}"
            -Exe "${CMAKE_BINARY_DIR}/src/ui/FastFiles.exe"
            -Filter all
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Run FastFiles real desktop E2E scenarios"
    USES_TERMINAL
  )
  set_target_properties(fastfiles_e2e PROPERTIES FOLDER "Tests/E2E")
endif()

set_property(SOURCE README.md PROPERTY HEADER_FILE_ONLY TRUE)
