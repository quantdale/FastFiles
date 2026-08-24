# phase8-release-readiness-convergence: machine-generated status fixture tests.
find_program(FF_PWSH_EXECUTABLE pwsh)
if(FF_PWSH_EXECUTABLE AND NOT EXISTS "${FF_PWSH_EXECUTABLE}")
  unset(FF_PWSH_EXECUTABLE CACHE)
  find_program(FF_PWSH_EXECUTABLE pwsh)
endif()
if(FF_PWSH_EXECUTABLE)
  add_test(
    NAME ffproject_status_ps_tests
    COMMAND "${FF_PWSH_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass -File
            "${PROJECT_SOURCE_DIR}/verify/status/tests/Test-ProjectStatus.ps1"
  )
  set_tests_properties(ffproject_status_ps_tests PROPERTIES TIMEOUT 120)
endif()
