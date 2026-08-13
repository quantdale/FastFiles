add_executable(fftest
  src/Main.cpp
)

fastfiles_add_version_resource(fftest "fftest.exe" "FastFiles diagnostic validation probe")

target_link_libraries(fftest PRIVATE ffprotocol ffipc ffsetup ffindexsvcprobes ffindexstore)
