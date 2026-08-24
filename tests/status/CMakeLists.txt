# phase8-release-readiness-convergence: machine-generated status fixture tests.
include("${PROJECT_SOURCE_DIR}/cmake/FastFilesPowerShell.cmake")
ff_resolve_powershell(FF_PWSH_EXECUTABLE)
if(FF_PWSH_EXECUTABLE)
  add_test(
    NAME ffproject_status_ps_tests
    COMMAND "${FF_PWSH_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass -File
            "${PROJECT_SOURCE_DIR}/verify/status/tests/Test-ProjectStatus.ps1"
  )
  set_tests_properties(ffproject_status_ps_tests PROPERTIES TIMEOUT 120)
endif()
