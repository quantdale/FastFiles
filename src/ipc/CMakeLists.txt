add_library(ffipc STATIC
  src/PipeFraming.cpp
  src/PipeListener.cpp
)

set(FF_IPC_PUBLIC_HEADERS
  include/ffipc/PipeFraming.h
  include/ffipc/PipeListener.h
)
target_sources(ffipc PRIVATE ${FF_IPC_PUBLIC_HEADERS})
source_group("Public API" FILES ${FF_IPC_PUBLIC_HEADERS})
source_group("Implementation" FILES src/PipeFraming.cpp src/PipeListener.cpp)

target_include_directories(ffipc PUBLIC include)
target_compile_features(ffipc PUBLIC cxx_std_20)
target_link_libraries(ffipc PUBLIC ffprotocol)
