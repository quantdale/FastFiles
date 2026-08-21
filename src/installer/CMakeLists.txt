add_executable(FastFilesSetup
  src/Main.cpp
  src/InstallSteps.cpp
  src/ScratchPath.cpp
)

target_include_directories(FastFilesSetup PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

fastfiles_add_version_resource(FastFilesSetup "FastFilesSetup.exe" "FastFiles setup")

target_link_libraries(FastFilesSetup PRIVATE ffsetup ffprotocol wer)
