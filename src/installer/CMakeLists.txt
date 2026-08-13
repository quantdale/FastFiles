add_executable(FastFilesSetup
  src/Main.cpp
  src/InstallSteps.cpp
  src/ScratchPath.cpp
)

fastfiles_add_version_resource(FastFilesSetup "FastFilesSetup.exe" "FastFiles setup")

target_link_libraries(FastFilesSetup PRIVATE ffsetup ffprotocol wer)
