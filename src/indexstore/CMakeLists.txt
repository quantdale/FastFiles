add_library(ffindexstore STATIC
  src/Store.cpp
  src/NamePool.cpp
  src/Projection.cpp
)

target_include_directories(ffindexstore PUBLIC include)
target_compile_features(ffindexstore PUBLIC cxx_std_20)
target_link_libraries(ffindexstore PUBLIC sqlite3)
