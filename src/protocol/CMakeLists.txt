add_library(ffprotocol STATIC
  src/Frame.cpp
  src/Records.cpp
  src/Dispatch.cpp
  src/Version.cpp
)

target_include_directories(ffprotocol PUBLIC include)
target_compile_features(ffprotocol PUBLIC cxx_std_20)
