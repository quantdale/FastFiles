add_library(ffindexstore STATIC
  src/NamePool.cpp
  src/Projection.cpp
  src/DurableStore.cpp
  src/IndexStore.cpp
)

target_include_directories(ffindexstore PUBLIC include)
target_compile_features(ffindexstore PUBLIC cxx_std_20)
target_link_libraries(ffindexstore PUBLIC ffprotocol PRIVATE sqlite3)
