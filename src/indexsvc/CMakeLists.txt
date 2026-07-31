add_executable(FastFilesIndexSvc
  src/Main.cpp
  src/DllHardening.cpp
  src/DiagnosticsHardening.cpp
  src/StalenessMonitor.cpp
  src/VolumeEnumeration.cpp
  src/ClientAuthentication.cpp
  src/ConnectionRegistry.cpp
  src/PrivilegeVerification.cpp
  src/ServiceConnection.cpp
)

target_link_libraries(FastFilesIndexSvc PRIVATE ffprotocol ffipc ffsetup)
