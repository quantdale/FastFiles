# runtime-certification-and-verifier-convergence 8.1: verifier result and
# worker-boundary fixtures are plain PowerShell tests and use a temp fixture
# root; they never consume ignored verify/runs artifacts.
include("${PROJECT_SOURCE_DIR}/cmake/FastFilesPowerShell.cmake")
ff_resolve_powershell(FF_PWSH_EXECUTABLE)
if(FF_PWSH_EXECUTABLE)
  add_test(
    NAME ffverify_result_composition_ps_tests
    COMMAND "${FF_PWSH_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass -File
            "${PROJECT_SOURCE_DIR}/verify/core/tests/Test-ResultComposition.ps1"
  )
  set_tests_properties(ffverify_result_composition_ps_tests PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77)
  add_test(
    NAME ffverify_uia_selection_ps_tests
    COMMAND "${FF_PWSH_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass -File
            "${PROJECT_SOURCE_DIR}/verify/core/tests/Test-UiaScenarioSelection.ps1"
  )
  set_tests_properties(ffverify_uia_selection_ps_tests PROPERTIES TIMEOUT 30)
  add_test(
    NAME ffverify_process_reaper_ps_tests
    COMMAND "${FF_PWSH_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass -File
            "${PROJECT_SOURCE_DIR}/verify/core/tests/Test-ProcessReaper.ps1"
  )
  set_tests_properties(ffverify_process_reaper_ps_tests PROPERTIES TIMEOUT 60)
  # Real desktop E2E remains an explicit dev.ps1 action; this headless
  # contract test keeps its marker and bounded-wait helpers covered by CTest.
  add_test(
    NAME ffe2e_harness_ps_tests
    COMMAND "${FF_PWSH_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass -File
            "${PROJECT_SOURCE_DIR}/tests/e2e/Test-E2EHarness.ps1"
  )
  set_tests_properties(ffe2e_harness_ps_tests PROPERTIES TIMEOUT 120)
endif()
