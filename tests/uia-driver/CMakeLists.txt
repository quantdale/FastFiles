# zero-touch-autonomous-engineering (uia-driver): registers the headless PowerShell
# driver unit tests with CTest. Test name == target name (ffuia_driver_ps_tests);
# matching via ctest -R uia_driver.
find_program(FF_PWSH_EXECUTABLE pwsh)
if(FF_PWSH_EXECUTABLE)
  add_test(
    NAME ffuia_driver_ps_tests
    COMMAND "${FF_PWSH_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass -File
            "${PROJECT_SOURCE_DIR}/verify/uia-driver/tests/run-tests.ps1"
  )
  set_tests_properties(ffuia_driver_ps_tests PROPERTIES TIMEOUT 180)
endif()
