# Governance / read-only session tests (pwsh-gated; hermetic temp git repos).
include("${PROJECT_SOURCE_DIR}/cmake/FastFilesPowerShell.cmake")
ff_resolve_powershell(FF_PWSH_EXECUTABLE)
if(FF_PWSH_EXECUTABLE)
  add_test(
    NAME ffreadonly_mode_ps_tests
    COMMAND "${FF_PWSH_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass -File
            "${PROJECT_SOURCE_DIR}/verify/governance/tests/Test-ReadOnlyMode.ps1"
  )
  set_tests_properties(ffreadonly_mode_ps_tests PROPERTIES TIMEOUT 120)

  add_test(
    NAME ffgit_mutation_guard_ps_tests
    COMMAND "${FF_PWSH_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass -File
            "${PROJECT_SOURCE_DIR}/verify/governance/tests/Test-GitMutationGuard.ps1"
  )
  set_tests_properties(ffgit_mutation_guard_ps_tests PROPERTIES TIMEOUT 120)

  add_test(
    NAME ffmission_precedence_ps_tests
    COMMAND "${FF_PWSH_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass -File
            "${PROJECT_SOURCE_DIR}/verify/governance/tests/Test-MissionPrecedence.ps1"
  )
  set_tests_properties(ffmission_precedence_ps_tests PROPERTIES TIMEOUT 60)

  add_test(
    NAME ffreadonly_terminal_ps_tests
    COMMAND "${FF_PWSH_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass -File
            "${PROJECT_SOURCE_DIR}/verify/governance/tests/Test-ReadOnlyTerminalAssertion.ps1"
  )
  set_tests_properties(ffreadonly_terminal_ps_tests PROPERTIES TIMEOUT 120)

  add_test(
    NAME ffhook_guard_ps_tests
    COMMAND "${FF_PWSH_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass -File
            "${PROJECT_SOURCE_DIR}/verify/governance/tests/Test-HookGuard.ps1"
  )
  set_tests_properties(ffhook_guard_ps_tests PROPERTIES TIMEOUT 60)
endif()
