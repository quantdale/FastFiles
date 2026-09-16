# Raw MFT record byte-parsing (index-storage-and-scanning tasks.md
# 4.1/4.2) has no Win32/DeviceIoControl dependency, split into its own
# static library so it can be unit tested (tests/indexsvc) independently
# of the Windows-only service executable.
add_library(ffmftparser STATIC
  src/MftParser.cpp
)
target_sources(ffmftparser PRIVATE src/MftParser.h)
source_group("Parser/Implementation" FILES src/MftParser.cpp)
source_group("Parser/Private Headers" FILES src/MftParser.h)
target_include_directories(ffmftparser PUBLIC src)
target_compile_features(ffmftparser PUBLIC cxx_std_20)

add_library(ffusnparser STATIC
  src/UsnRecordParser.cpp
)
target_sources(ffusnparser PRIVATE src/UsnRecordParser.h)
source_group("Parser/Implementation" FILES src/UsnRecordParser.cpp)
source_group("Parser/Private Headers" FILES src/UsnRecordParser.h)
target_include_directories(ffusnparser PUBLIC src)
target_compile_features(ffusnparser PUBLIC cxx_std_20)

add_library(ffindexsvcprobes STATIC
  src/PrivilegeVerification.cpp
  src/VolumeEnumeration.cpp
  src/ClientAuthentication.cpp
)
set(FF_INDEXSVC_PROBE_HEADERS
  src/ClientAuthentication.h
  src/PrivilegeVerification.h
  src/VolumeEnumeration.h
)
target_sources(ffindexsvcprobes PRIVATE ${FF_INDEXSVC_PROBE_HEADERS})
source_group("Service Probes/Implementation" FILES
  src/PrivilegeVerification.cpp src/VolumeEnumeration.cpp src/ClientAuthentication.cpp)
source_group("Service Probes/Private Headers" FILES ${FF_INDEXSVC_PROBE_HEADERS})
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

set(FF_INDEXSVC_PRIVATE_HEADERS
  src/CommandSurface.h
  src/ConnectionRegistry.h
  src/DiagnosticsHardening.h
  src/DllHardening.h
  src/ServiceConnection.h
  src/StalenessMonitor.h
  src/UsnJournalReader.h
  src/VolumeScanner.h
  src/WriteDeadline.h
)
target_sources(FastFilesIndexSvc PRIVATE ${FF_INDEXSVC_PRIVATE_HEADERS})
source_group("Service/Implementation" FILES
  src/Main.cpp src/DllHardening.cpp src/DiagnosticsHardening.cpp
  src/StalenessMonitor.cpp src/ConnectionRegistry.cpp src/ServiceConnection.cpp
  src/VolumeScanner.cpp src/UsnJournalReader.cpp)
source_group("Service/Private Headers" FILES ${FF_INDEXSVC_PRIVATE_HEADERS})
set_target_properties(FastFilesIndexSvc PROPERTIES FOLDER "App/Service")

fastfiles_add_version_resource(FastFilesIndexSvc "FastFilesIndexSvc.exe" "FastFiles privileged index service")

target_link_libraries(FastFilesIndexSvc PRIVATE ffprotocol ffipc ffsetup ffmftparser ffusnparser ffindexsvcprobes)
