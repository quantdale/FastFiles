add_library(ffsearch STATIC src/Query.cpp src/Search.cpp src/History.cpp src/Path.cpp)

set(FF_SEARCH_PUBLIC_HEADERS
  include/ffsearch/History.h
  include/ffsearch/Path.h
  include/ffsearch/Query.h
  include/ffsearch/Search.h
)
target_sources(ffsearch PRIVATE ${FF_SEARCH_PUBLIC_HEADERS})
source_group("Public API" FILES ${FF_SEARCH_PUBLIC_HEADERS})
source_group("Implementation" FILES src/Query.cpp src/Search.cpp src/History.cpp src/Path.cpp)
target_include_directories(ffsearch PUBLIC include)
target_link_libraries(ffsearch PUBLIC ffprotocol)
target_compile_features(ffsearch PUBLIC cxx_std_20)
