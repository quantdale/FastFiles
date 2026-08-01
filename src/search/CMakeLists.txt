add_library(ffsearch STATIC src/Query.cpp)
target_include_directories(ffsearch PUBLIC include)
target_link_libraries(ffsearch PUBLIC ffprotocol)
target_compile_features(ffsearch PUBLIC cxx_std_20)
