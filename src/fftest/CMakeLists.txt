add_executable(fftest
  src/Main.cpp
)

target_link_libraries(fftest PRIVATE ffprotocol ffipc ffsetup)
