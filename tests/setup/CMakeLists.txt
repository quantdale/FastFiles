# close-independent-validation-gaps 3.5: SDDL-level assertions on the five
# ffsetup security-descriptor builders. Test name == target name
# (ffsetup_security_descriptor_tests); matching via ctest -R setup.
add_executable(ffsetup_security_descriptor_tests test_security_descriptors.cpp)
target_link_libraries(ffsetup_security_descriptor_tests PRIVATE ffsetup)
add_test(NAME ffsetup_security_descriptor_tests COMMAND ffsetup_security_descriptor_tests)
set_tests_properties(ffsetup_security_descriptor_tests PROPERTIES TIMEOUT 120)
