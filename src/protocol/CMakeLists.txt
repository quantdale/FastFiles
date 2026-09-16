add_library(ffprotocol STATIC
  src/Frame.cpp
  src/Commands.cpp
  src/Records.cpp
  src/Dispatch.cpp
  src/Version.cpp
  src/UiProtocol.cpp
  src/IndexHealth.cpp
  src/Settings.cpp
  src/Diagnostics.cpp
  src/SnapshotFormat.cpp
)

set(FF_PROTOCOL_PUBLIC_HEADERS
  include/ffprotocol/Commands.h
  include/ffprotocol/Diagnostics.h
  include/ffprotocol/Dispatch.h
  include/ffprotocol/Frame.h
  include/ffprotocol/IndexHealth.h
  include/ffprotocol/Records.h
  include/ffprotocol/Settings.h
  include/ffprotocol/SnapshotFormat.h
  include/ffprotocol/UiProtocol.h
  include/ffprotocol/Version.h
  include/ffprotocol/WindowsPath.h
)
target_sources(ffprotocol PRIVATE ${FF_PROTOCOL_PUBLIC_HEADERS})
source_group("Public API" FILES ${FF_PROTOCOL_PUBLIC_HEADERS})
source_group("Implementation" FILES
  src/Frame.cpp src/Commands.cpp src/Records.cpp src/Dispatch.cpp src/Version.cpp
  src/UiProtocol.cpp src/IndexHealth.cpp src/Settings.cpp src/Diagnostics.cpp
  src/SnapshotFormat.cpp)

target_include_directories(ffprotocol PUBLIC include)
target_compile_features(ffprotocol PUBLIC cxx_std_20)
