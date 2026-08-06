# Doc-drift guard, registered only when pwsh is available (mirrors
# tests/uia-driver/CMakeLists.txt's gating). Test name == target name
# (ffdoc_drift_tests); matching via ctest -R drift.
find_program(FF_PWSH_EXECUTABLE pwsh)
if(FF_PWSH_EXECUTABLE)
  add_test(
    NAME ffdoc_drift_tests
    COMMAND "${FF_PWSH_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass -File
            "${PROJECT_SOURCE_DIR}/tests/drift/check_doc_drift.ps1"
  )
  set_tests_properties(ffdoc_drift_tests PROPERTIES TIMEOUT 120)
endif()
