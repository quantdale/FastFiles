add_executable(FastFiles WIN32
  src/Main.cpp
  src/WindowShell.cpp
  src/Renderer.cpp
  src/ColumnView.cpp
  src/EngineClient.cpp
)

target_link_libraries(FastFiles PRIVATE
  ffprotocol
  ffipc
  ffsetup
  d3d11
  dxgi
  d2d1
  dwrite
  dcomp
)
