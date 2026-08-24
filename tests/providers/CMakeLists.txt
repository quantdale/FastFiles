# phase8-release-readiness-convergence: deterministic provider orchestration tests.
include("${PROJECT_SOURCE_DIR}/cmake/FastFilesPowerShell.cmake")
ff_resolve_powershell(FF_PWSH_EXECUTABLE)
if(FF_PWSH_EXECUTABLE)
  add_test(
    NAME ffprovider_contract_ps_tests
    COMMAND "${FF_PWSH_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass -File
            "${PROJECT_SOURCE_DIR}/verify/providers/tests/Test-ProviderContract.ps1"
  )
  set_tests_properties(ffprovider_contract_ps_tests PROPERTIES TIMEOUT 120)
endif()
