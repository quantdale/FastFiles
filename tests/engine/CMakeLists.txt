add_executable(ffengine_index_pipeline_tests
  test_index_pipeline.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/src/IndexPipeline.cpp
)
target_include_directories(ffengine_index_pipeline_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/engine/src)
target_link_libraries(ffengine_index_pipeline_tests PRIVATE ffindexstore ffprotocol)

add_test(NAME ffengine_index_pipeline_tests COMMAND ffengine_index_pipeline_tests)

add_executable(ffengine_volume_session_manager_tests
  test_volume_session_manager.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/src/IndexPipeline.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/src/PrivilegedConnection.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/src/VolumeIdentity.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/src/VolumeSessionManager.cpp
)
target_include_directories(ffengine_volume_session_manager_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/engine/src)
target_link_libraries(ffengine_volume_session_manager_tests PRIVATE ffindexstore ffprotocol ffipc ffsetup Rpcrt4 Shell32 Ole32)
add_test(NAME ffengine_volume_session_manager_tests COMMAND ffengine_volume_session_manager_tests)

add_executable(ffengine_authenticode_verification_tests
  test_authenticode_verification.cpp
)
target_include_directories(ffengine_authenticode_verification_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/engine/src)
target_link_libraries(ffengine_authenticode_verification_tests PRIVATE ffsetup)
add_test(NAME ffengine_authenticode_verification_tests COMMAND ffengine_authenticode_verification_tests)

add_executable(ffengine_degraded_special_files_tests
  test_degraded_special_files.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/src/DegradedModeEnumerator.cpp
)
target_include_directories(ffengine_degraded_special_files_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/engine/src)
target_link_libraries(ffengine_degraded_special_files_tests PRIVATE shell32)
add_test(NAME ffengine_degraded_special_files_tests COMMAND ffengine_degraded_special_files_tests)

# zero-touch-autonomous-engineering: subtree-gating unit tests (closes
# settings-and-appearance 2.5). Compiles IndexPipeline.cpp directly so the
# engine's rule-honoring ingestion is testable without the GUI/service.
add_executable(ffengine_subtree_gating_tests
  test_subtree_gating.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/src/IndexPipeline.cpp
)
target_include_directories(ffengine_subtree_gating_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/engine/src)
target_link_libraries(ffengine_subtree_gating_tests PRIVATE ffindexstore ffprotocol)
add_test(NAME ffengine_subtree_gating_tests COMMAND ffengine_subtree_gating_tests)
