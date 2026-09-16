add_library(ffindexstore STATIC
  src/Store.cpp
  src/NamePool.cpp
  src/Projection.cpp
)

set(FF_INDEXSTORE_PUBLIC_HEADERS
  include/ffindexstore/EntryRecord.h
  include/ffindexstore/FlatHashMap.h
  include/ffindexstore/Identity.h
  include/ffindexstore/NamePool.h
  include/ffindexstore/Projection.h
  include/ffindexstore/Store.h
)
target_sources(ffindexstore PRIVATE ${FF_INDEXSTORE_PUBLIC_HEADERS})
source_group("Public API" FILES ${FF_INDEXSTORE_PUBLIC_HEADERS})
source_group("Implementation" FILES src/Store.cpp src/NamePool.cpp src/Projection.cpp)

target_include_directories(ffindexstore PUBLIC include)
target_compile_features(ffindexstore PUBLIC cxx_std_20)
target_link_libraries(ffindexstore PUBLIC sqlite3)
