add_executable(ffprotocol_tests test_protocol.cpp)
target_link_libraries(ffprotocol_tests PRIVATE ffprotocol)

add_test(NAME ffprotocol_tests COMMAND ffprotocol_tests)
