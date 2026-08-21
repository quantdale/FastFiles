# close-independent-validation-gaps 3.5: SDDL-level assertions on the five
# ffsetup security-descriptor builders. Test name == target name
# (ffsetup_security_descriptor_tests); matching via ctest -R setup.
add_executable(ffsetup_security_descriptor_tests test_security_descriptors.cpp)
target_link_libraries(ffsetup_security_descriptor_tests PRIVATE ffsetup)
add_test(NAME ffsetup_security_descriptor_tests COMMAND ffsetup_security_descriptor_tests)
set_tests_properties(ffsetup_security_descriptor_tests PROPERTIES TIMEOUT 120)

# close-independent-validation-gaps 4.2: the service and engine may observe
# the same installed image through normal or extended-length Win32 spelling.
add_executable(ffsetup_peer_image_match_tests test_peer_image_match.cpp)
target_link_libraries(ffsetup_peer_image_match_tests PRIVATE ffsetup)
add_test(NAME ffsetup_peer_image_match_tests COMMAND ffsetup_peer_image_match_tests)
set_tests_properties(ffsetup_peer_image_match_tests PROPERTIES TIMEOUT 120)

# fastfiles-audit-remediation FF-AUDIT-01/GAP-B: handle-bound package
# verification and staging regression coverage. Signature verification is
# injected in the test so unsigned/wrong/unapproved/approved signer outcomes
# are deterministic without manufacturing a production certificate.
#
# Portable suite: no elevation, no production signer/pins, no trust-store
# mutation. This is the mandatory default; it must keep passing on an
# ordinary non-admin Windows host and in hosted CI.
add_executable(ffsetup_package_trust_tests test_package_trust.cpp)
target_link_libraries(ffsetup_package_trust_tests PRIVATE ffsetup)
add_test(NAME ffsetup_package_trust_tests COMMAND ffsetup_package_trust_tests)
set_tests_properties(ffsetup_package_trust_tests PROPERTIES TIMEOUT 300)

# Privileged CurrentUser\Root RFC3161 detached-CMS integration suite: opt-in
# only, not part of the default CTest run. See AGENTS.md "Normal FastFiles
# development" and verify/run_privileged_trust_tests.ps1.
if(FASTFILES_ENABLE_PRIVILEGED_TRUST_TESTS)
  add_executable(ffsetup_package_trust_wintrust_integration_tests test_package_trust_wintrust_integration.cpp)
  target_link_libraries(ffsetup_package_trust_wintrust_integration_tests PRIVATE ffsetup)
  add_custom_target(ffsetup_package_trust_wintrust_fixture ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/PackageTrustFixture.ps1"
      "$<TARGET_FILE_DIR:ffsetup_package_trust_wintrust_integration_tests>/PackageTrustFixture.ps1"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/PackageTrustFixture.ps1"
  )
  add_dependencies(ffsetup_package_trust_wintrust_integration_tests ffsetup_package_trust_wintrust_fixture)
  add_test(NAME ffsetup_package_trust_wintrust_integration_tests
    COMMAND ffsetup_package_trust_wintrust_integration_tests)
  set_tests_properties(ffsetup_package_trust_wintrust_integration_tests
    PROPERTIES TIMEOUT 300 LABELS privileged-trust)
endif()

# fastfiles-audit-remediation FF-AUDIT-02: source-level guard that prevents
# reintroducing an elevated %TEMP% relay or detached copied-helper teardown.
add_executable(ffsetup_installer_source_policy_tests test_installer_source_policy.cpp)
add_test(NAME ffsetup_installer_source_policy_tests COMMAND ffsetup_installer_source_policy_tests)
set_tests_properties(ffsetup_installer_source_policy_tests PROPERTIES TIMEOUT 120)

# Installer lifecycle decision logic (audit campaign, installer area):
# startup-token verdicts, self-replacement detection, fresh-install rollback
# bookkeeping, service-stop wait policy, and the single-instance mutex --
# all pure seams, no elevation and no SCM/process side effects.
add_executable(ffsetup_install_lifecycle_tests test_install_lifecycle.cpp)
target_link_libraries(ffsetup_install_lifecycle_tests PRIVATE ffsetup)
target_include_directories(ffsetup_install_lifecycle_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/src/installer/include)
add_test(NAME ffsetup_install_lifecycle_tests COMMAND ffsetup_install_lifecycle_tests)
set_tests_properties(ffsetup_install_lifecycle_tests PROPERTIES TIMEOUT 120)
