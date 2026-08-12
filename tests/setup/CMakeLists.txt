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
