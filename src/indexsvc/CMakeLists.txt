# Raw MFT record byte-parsing (index-storage-and-scanning tasks.md
# 4.1/4.2) has no Win32/DeviceIoControl dependency, split into its own
# static library so it can be unit tested (tests/indexsvc) independently
# of the Windows-only service executable.
add_library(ffmftparser STATIC
  src/MftParser.cpp
)
target_include_directories(ffmftparser PUBLIC src)
target_compile_features(ffmftparser PUBLIC cxx_std_20)

add_library(ffindexsvcprobes STATIC
  src/PrivilegeVerification.cpp
  src/VolumeEnumeration.cpp
  src/ClientAuthentication.cpp
)
target_include_directories(ffindexsvcprobes PUBLIC src)
target_compile_features(ffindexsvcprobes PUBLIC cxx_std_20)
target_link_libraries(ffindexsvcprobes PUBLIC ffprotocol ffsetup)

add_executable(FastFilesIndexSvc
  src/Main.cpp
  src/DllHardening.cpp
  src/DiagnosticsHardening.cpp
  src/StalenessMonitor.cpp
  src/ConnectionRegistry.cpp
  src/ServiceConnection.cpp
  src/VolumeScanner.cpp
  src/UsnJournalReader.cpp
)

target_link_libraries(FastFilesIndexSvc PRIVATE ffprotocol ffipc ffsetup ffmftparser ffindexsvcprobes)
