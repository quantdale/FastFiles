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
  # Source-to-binary provenance gate: stale/mixed/missing/malformed identities
  # and dirty-tree exact-mode must fail closed with PROVENANCE_MISMATCH.
  add_test(
    NAME ffverify_provenance_ps_tests
    COMMAND "${FF_PWSH_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass -File
            "${PROJECT_SOURCE_DIR}/verify/core/tests/Test-Provenance.ps1"
  )
  set_tests_properties(ffverify_provenance_ps_tests PROPERTIES TIMEOUT 120)
  # hardening-campaign Phase 4: the foreground-skip converter must require
  # proven foreign ownership or a blocked input point; missing evidence is a
  # real failure, never an environment skip.
  add_test(
    NAME ffverify_foreground_skip_ps_tests
    COMMAND "${FF_PWSH_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass -File
            "${PROJECT_SOURCE_DIR}/verify/core/tests/Test-ForegroundSkipConversion.ps1"
  )
  set_tests_properties(ffverify_foreground_skip_ps_tests PROPERTIES TIMEOUT 60)
  # hardening-campaign Phase 8: workflow<->local-parity drift contract; a
  # ci.yml job change fails this test until the orchestrator manifest and
  # hosted refs are updated together.
  add_test(
    NAME ffverify_parity_contract_ps_tests
    COMMAND "${FF_PWSH_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass -File
            "${PROJECT_SOURCE_DIR}/verify/core/tests/Test-CiParityContract.ps1"
  )
  set_tests_properties(ffverify_parity_contract_ps_tests PROPERTIES TIMEOUT 60)
  # hardening-campaign Phase 5: clean-host/install/service certification
  # harness (HostContamination.psm1 + InstallCertificationPlan.psm1) --
  # detector state transitions and the section-11 skeleton vocabulary guard,
  # fixture-driven through injectable probes; never touches real service,
  # group, registry, or filesystem markers.
  add_test(
    NAME ffverify_clean_host_harness_ps_tests
    COMMAND "${FF_PWSH_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass -File
            "${PROJECT_SOURCE_DIR}/verify/core/tests/Test-CleanHostHarness.ps1"
  )
  set_tests_properties(ffverify_clean_host_harness_ps_tests PROPERTIES TIMEOUT 60)
  # hardening-campaign Phase 6: DPI/display/accessibility certification
  # MATRIX harness (HostDisplayCapabilities.psm1 + Invoke-DisplayMatrix.ps1):
  # injectable detection fixtures and requested-vs-executed evidence shape;
  # launches nothing, mutates no host setting, reads no run artifacts.
  add_test(
    NAME ffverify_display_matrix_harness_ps_tests
    COMMAND "${FF_PWSH_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass -File
            "${PROJECT_SOURCE_DIR}/verify/core/tests/Test-DisplayMatrixHarness.ps1"
  )
  set_tests_properties(ffverify_display_matrix_harness_ps_tests PROPERTIES TIMEOUT 60)
endif()
