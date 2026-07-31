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

target_link_libraries(FastFilesEngine PRIVATE ffprotocol ffipc ffsetup ffindexstore Rpcrt4 Shell32 Ole32)
