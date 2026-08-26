# Doc-drift guard, registered only when pwsh is available (mirrors
# tests/uia-driver/CMakeLists.txt's gating). Test name == target name
# (ffdoc_drift_tests); matching via ctest -R drift.
include("${PROJECT_SOURCE_DIR}/cmake/FastFilesPowerShell.cmake")
ff_resolve_powershell(FF_PWSH_EXECUTABLE)
if(FF_PWSH_EXECUTABLE)
  add_test(
    NAME ffdoc_drift_tests
    COMMAND "${FF_PWSH_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass -File
            "${PROJECT_SOURCE_DIR}/tests/drift/check_doc_drift.ps1"
  )
  set_tests_properties(ffdoc_drift_tests PROPERTIES TIMEOUT 120)

  # Adversarial fixture matrix for the semantic truth contracts (hermetic
  # temp trees; no repo mutation, no network).
  add_test(
    NAME ffdoc_truth_fixture_tests
    COMMAND "${FF_PWSH_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass -File
            "${PROJECT_SOURCE_DIR}/tests/drift/test_doc_truth_fixtures.ps1"
  )
  set_tests_properties(ffdoc_truth_fixture_tests PROPERTIES TIMEOUT 300)
endif()
