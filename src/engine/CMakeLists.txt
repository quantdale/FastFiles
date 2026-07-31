add_executable(FastFilesEngine
  src/Main.cpp
  src/PrivilegedConnection.cpp
  src/DegradedModeEnumerator.cpp
  src/DirectoryWatcher.cpp
  src/SnapshotPublisher.cpp
  src/UiServer.cpp
  src/IdleManager.cpp
  src/IngestionPipeline.cpp
  src/VolumeIdentityLookup.cpp
)

target_link_libraries(FastFilesEngine PRIVATE ffprotocol ffipc ffsetup ffindexstore)
