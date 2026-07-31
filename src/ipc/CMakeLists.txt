add_library(ffipc STATIC
  src/PipeFraming.cpp
  src/PipeListener.cpp
)

target_include_directories(ffipc PUBLIC include)
target_compile_features(ffipc PUBLIC cxx_std_20)
target_link_libraries(ffipc PUBLIC ffprotocol)
