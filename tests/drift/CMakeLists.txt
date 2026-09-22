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

  # Repository-state provenance fixtures use disposable Git repositories to
  # prove dirty development results cannot qualify a different committed tree.
  add_test(
    NAME ffsource_provenance_fixture_tests
    COMMAND "${FF_PWSH_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass -File
            "${PROJECT_SOURCE_DIR}/tests/drift/test_source_provenance_fixtures.ps1"
  )
  set_tests_properties(ffsource_provenance_fixture_tests PROPERTIES TIMEOUT 180)

  # Build-identity freshness: prove the build-time identity script refreshes
  # PrivateBuild / kBuildIdentity when HEAD moves without a reconfigure, and
  # that malformed/missing identities fail closed.
  add_test(
    NAME ffbuild_identity_freshness_tests
    COMMAND "${FF_PWSH_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass -File
            "${PROJECT_SOURCE_DIR}/tests/drift/test_build_identity_freshness.ps1"
  )
  set_tests_properties(ffbuild_identity_freshness_tests PROPERTIES TIMEOUT 180)

  # Binary identity binding: the five debug product binaries must embed the
  # current source identity as PrivateBuild (skip 77 until the debug preset is
  # built). This is the detector that would have caught the stale-PrivateBuild
  # evidence incident before certification claimed exact-tip binaries.
  add_test(
    NAME ffbuild_identity_binding_tests
    COMMAND "${FF_PWSH_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass -File
            "${PROJECT_SOURCE_DIR}/tests/drift/test_binary_identity_binding.ps1"
  )
  set_tests_properties(ffbuild_identity_binding_tests PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
endif()
