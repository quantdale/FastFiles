add_executable(FastFiles WIN32
  src/Main.cpp
  src/FastFiles.rc
  src/WindowShell.cpp
  src/Renderer.cpp
  src/ColumnView.cpp
  src/EngineClient.cpp
  src/EngineDiscovery.cpp
  src/FileOperations.cpp
  src/FileOperationPolicy.cpp
  src/ConflictDialog.cpp
  src/OleDragDrop.cpp
  src/Preview.cpp
  src/NavigationWorkspace.cpp
  src/NavigationChrome.cpp
  src/NavigationSidebar.cpp
  src/CommandSystem.cpp
  src/CommandPalette.cpp
  src/QuickActions.cpp
  src/SearchPanel.cpp
  src/StorageAnalysis.cpp
  src/SettingsDialog.cpp
  src/CategoryEngine.cpp
  src/TreemapView.cpp
  src/Util.cpp
  src/UiStyle.cpp
  src/UiAnimation.cpp
  src/IconCache.cpp
)

target_include_directories(FastFiles PRIVATE include)

# The explicit RT_MANIFEST resource below carries Common Controls v6,
# PerMonitorV2, and longPathAware declarations.  Disable MSVC's automatic
# second manifest resource so the linker does not emit a duplicate RT_MANIFEST.
target_link_options(FastFiles PRIVATE "/MANIFEST:NO")

fastfiles_add_version_resource(FastFiles "FastFiles.exe" "FastFiles desktop shell")

target_link_libraries(FastFiles PRIVATE
  ffprotocol
  ffipc
  ffsetup
  ffsearch
  d3d11
  dxgi
  d2d1
  dwrite
  dcomp
  windowscodecs
  shell32
  ole32
  comctl32
  comdlg32
)
