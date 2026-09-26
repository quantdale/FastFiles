add_executable(ffipc_framing_tests test_pipe_framing.cpp)
target_sources(ffipc_framing_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src/ipc/src/PipeFraming.cpp)
target_include_directories(ffipc_framing_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src/ipc/include)
target_compile_definitions(ffipc_framing_tests PRIVATE FF_IPC_TESTING=1)
target_link_libraries(ffipc_framing_tests PRIVATE ffprotocol)

add_test(NAME ffipc_framing_tests COMMAND ffipc_framing_tests)
set_tests_properties(ffipc_framing_tests PROPERTIES TIMEOUT 120)

add_executable(ffipc_listener_lifecycle_tests test_pipe_listener_lifecycle.cpp)
target_link_libraries(ffipc_listener_lifecycle_tests PRIVATE ffipc)
# Audit regression: exposes the retained-thread registry size for the
# finished-thread reaping test only.
target_compile_definitions(ffipc_listener_lifecycle_tests PRIVATE FF_PIPE_LISTENER_TESTING=1)

add_test(NAME ffipc_listener_lifecycle_tests COMMAND ffipc_listener_lifecycle_tests)
set_tests_properties(ffipc_listener_lifecycle_tests PROPERTIES TIMEOUT 120)
