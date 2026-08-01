add_executable(FastFilesSetup
  src/Main.cpp
  src/InstallSteps.cpp
  src/ScratchPath.cpp
)

target_link_libraries(FastFilesSetup PRIVATE ffsetup wer)
