# MftRecordParser is pure, portable, dependency-free parsing logic (no
# Win32 types) deliberately kept that way so it's unit-testable in
# isolation from the rest of FastFilesIndexSvc, which needs a live
# service process/raw volume access to exercise at all.
add_executable(ffmftrecordparser_tests
  test_mftrecordparser.cpp
  ${CMAKE_SOURCE_DIR}/src/indexsvc/src/MftRecordParser.cpp
)
target_include_directories(ffmftrecordparser_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/indexsvc/src)

add_test(NAME ffmftrecordparser_tests COMMAND ffmftrecordparser_tests)
