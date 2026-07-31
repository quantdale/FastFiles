add_executable(ffengine_index_pipeline_tests
  test_index_pipeline.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/src/IndexPipeline.cpp
)
target_include_directories(ffengine_index_pipeline_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/engine/src)
target_link_libraries(ffengine_index_pipeline_tests PRIVATE ffindexstore ffprotocol)

add_test(NAME ffengine_index_pipeline_tests COMMAND ffengine_index_pipeline_tests)
