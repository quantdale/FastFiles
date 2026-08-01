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
