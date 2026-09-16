add_executable(FastFilesEngine
  src/Main.cpp
  src/PrivilegedConnection.cpp
  src/DegradedModeEnumerator.cpp
  src/DirectoryWatcher.cpp
  src/SnapshotPublisher.cpp
  src/UiServer.cpp
  src/IdleManager.cpp
  src/IndexPipeline.cpp
  src/VolumeIdentity.cpp
  src/VolumeSessionManager.cpp
)

set(FF_ENGINE_PRIVATE_HEADERS
  src/DegradedModeEnumerator.h
  src/DirectoryWatcher.h
  src/IdleManager.h
  src/IndexPipeline.h
  src/PrivilegedConnection.h
  src/SnapshotPublisher.h
  src/UiServer.h
  src/VolumeIdentity.h
  src/VolumeSessionManager.h
)
target_sources(FastFilesEngine PRIVATE ${FF_ENGINE_PRIVATE_HEADERS})
source_group("Engine/Implementation" FILES
  src/Main.cpp src/PrivilegedConnection.cpp src/DegradedModeEnumerator.cpp
  src/DirectoryWatcher.cpp src/SnapshotPublisher.cpp src/UiServer.cpp
  src/IdleManager.cpp src/IndexPipeline.cpp src/VolumeIdentity.cpp
  src/VolumeSessionManager.cpp)
source_group("Engine/Private Headers" FILES ${FF_ENGINE_PRIVATE_HEADERS})
set_target_properties(FastFilesEngine PROPERTIES FOLDER "App/Engine")

fastfiles_add_version_resource(FastFilesEngine "FastFilesEngine.exe" "FastFiles index engine")

target_link_libraries(FastFilesEngine PRIVATE ffprotocol ffipc ffsetup ffindexstore ffsearch Rpcrt4 Shell32 Ole32)
