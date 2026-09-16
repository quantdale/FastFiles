add_executable(FastFilesSetup
  src/Main.cpp
  src/InstallSteps.cpp
  src/ScratchPath.cpp
)

set(FF_INSTALLER_PUBLIC_HEADERS include/ffinstaller/InstallLifecycle.h)
target_sources(FastFilesSetup PRIVATE ${FF_INSTALLER_PUBLIC_HEADERS})
source_group("Installer/Implementation" FILES src/Main.cpp src/InstallSteps.cpp src/ScratchPath.cpp)
source_group("Installer/Public API" FILES ${FF_INSTALLER_PUBLIC_HEADERS})
set_target_properties(FastFilesSetup PROPERTIES FOLDER "App/Installer")

target_include_directories(FastFilesSetup PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

fastfiles_add_version_resource(FastFilesSetup "FastFilesSetup.exe" "FastFiles setup")

target_link_libraries(FastFilesSetup PRIVATE ffsetup ffprotocol wer)
