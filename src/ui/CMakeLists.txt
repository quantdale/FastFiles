set(FF_UI_SHELL_SOURCES
  src/Main.cpp
  src/FastFiles.rc
  src/WindowShell.cpp
  src/WindowShell.h
  src/Renderer.cpp
  src/Renderer.h
  src/EngineClient.cpp
  src/EngineClient.h
  src/EngineDiscovery.cpp
  src/EngineDiscovery.h
  src/EngineStatusPolicy.h
  src/NativeChrome.h
  src/NativeChromePaint.h
  src/UiComponents.h
)

set(FF_UI_NAVIGATION_SOURCES
  src/NavigationWorkspace.cpp
  src/NavigationWorkspace.h
  src/RecentFolders.cpp
  src/RecentFolders.h
  src/NavigationChrome.cpp
  src/NavigationChrome.h
  src/NavigationSidebar.cpp
  src/NavigationSidebar.h
  src/NavigationLocation.h
  src/HomeData.h
)

set(FF_UI_BROWSER_SOURCES
  src/ColumnView.cpp
  src/ColumnView.h
  src/SelectionModel.h
)

set(FF_UI_ACCESSIBILITY_SOURCES
  src/UiaProviderBase.cpp
  src/UiaProviderBase.h
  src/UiaHostQuery.cpp
  src/UiaHostQuery.h
  src/ColumnViewUiaProvider.cpp
  src/ColumnViewUiaProvider.h
  src/UiaGeometry.h
  src/UiaNotifications.h
  src/UiaRuntimeIds.h
)

set(FF_UI_SEARCH_SOURCES
  src/SearchPanel.cpp
  src/SearchPanel.h
  src/SearchUiaProvider.cpp
  src/SearchUiaProvider.h
  src/SearchButtonUiaProvider.cpp
  src/SearchButtonUiaProvider.h
)

set(FF_UI_STORAGE_SOURCES
  src/CategoryEngine.cpp
  src/CategoryEngine.h
  src/StorageAnalysis.cpp
  src/StorageAnalysis.h
  src/TreemapFilter.h
  src/TreemapView.cpp
  src/TreemapView.h
  src/TreemapUiaProvider.cpp
  src/TreemapUiaProvider.h
)

set(FF_UI_FILE_OPERATION_SOURCES
  src/FileOperations.cpp
  src/FileOperations.h
  src/FileOperationPolicy.cpp
  src/FileOperationPolicy.h
  src/ConflictDialog.cpp
  src/ConflictDialog.h
  src/OleDragDrop.cpp
  src/OleDragDrop.h
  src/QuickActions.cpp
  src/QuickActions.h
)

set(FF_UI_PREVIEW_SOURCES
  src/Preview.cpp
  src/Preview.h
)

set(FF_UI_COMMAND_SOURCES
  src/CommandSystem.cpp
  src/CommandSystem.h
  src/CommandPalette.cpp
  src/CommandPalette.h
  src/SettingsDialog.cpp
  src/SettingsDialog.h
  src/Gallery.cpp
  src/Gallery.h
  src/UiStatePalette.h
)

set(FF_UI_PRESENTATION_SOURCES
  src/Util.cpp
  src/Util.h
  src/UiStyle.cpp
  src/UiStyle.h
  src/UiAnimation.cpp
  src/UiAnimation.h
  src/UITheme.h
  src/IconCache.cpp
  src/IconCache.h
)

# Public, renderer-free seams are listed in the target so Visual Studio shows
# the contracts beside the implementation that consumes them.  They remain
# installed only as source-tree headers; no install/export surface is added.
set(FF_UI_PUBLIC_HEADERS
  include/ffui/AddressBarFocus.h
  include/ffui/AsyncResultGate.h
  include/ffui/ChromeLayout.h
  include/ffui/DetailsAggregateState.h
  include/ffui/DetailsLayout.h
  include/ffui/DialogFocusReturn.h
  include/ffui/DisplayPath.h
  include/ffui/DropFeedbackLayout.h
  include/ffui/GuiShowCommand.h
  include/ffui/HomeLayout.h
  include/ffui/IconLru.h
  include/ffui/NativeOverlayLayout.h
  include/ffui/PaletteFocus.h
  include/ffui/ResponsiveLayout.h
  include/ffui/SearchFiltersDisclosure.h
  include/ffui/SearchLayout.h
  include/ffui/SearchResultMetadata.h
  include/ffui/SearchSnapshotRefresh.h
  include/ffui/SearchTabOrder.h
  include/ffui/SelectionDiagnostics.h
  include/ffui/SelectionIdentity.h
  include/ffui/StorageLayout.h
  include/ffui/StoragePercentage.h
  include/ffui/StorageRadioState.h
  include/ffui/StorageVisibility.h
  include/ffui/StorageRanking.h
  include/ffui/TreemapLayout.h
  include/ffui/TreemapSemantics.h
  include/ffui/UiaEvents.h
  include/ffui/UiaIdentity.h
  include/ffui/UiaSurface.h
)

add_executable(FastFiles WIN32
  ${FF_UI_SHELL_SOURCES}
  ${FF_UI_NAVIGATION_SOURCES}
  ${FF_UI_BROWSER_SOURCES}
  ${FF_UI_ACCESSIBILITY_SOURCES}
  ${FF_UI_SEARCH_SOURCES}
  ${FF_UI_STORAGE_SOURCES}
  ${FF_UI_FILE_OPERATION_SOURCES}
  ${FF_UI_PREVIEW_SOURCES}
  ${FF_UI_COMMAND_SOURCES}
  ${FF_UI_PRESENTATION_SOURCES}
  ${FF_UI_PUBLIC_HEADERS}
)

source_group("UI/Shell" FILES ${FF_UI_SHELL_SOURCES})
source_group("UI/Navigation" FILES ${FF_UI_NAVIGATION_SOURCES})
source_group("UI/Browser" FILES ${FF_UI_BROWSER_SOURCES})
source_group("UI/Accessibility" FILES ${FF_UI_ACCESSIBILITY_SOURCES})
source_group("UI/Search" FILES ${FF_UI_SEARCH_SOURCES})
source_group("UI/Storage Analysis" FILES ${FF_UI_STORAGE_SOURCES})
source_group("UI/File Operations" FILES ${FF_UI_FILE_OPERATION_SOURCES})
source_group("UI/Preview" FILES ${FF_UI_PREVIEW_SOURCES})
source_group("UI/Commands and Settings" FILES ${FF_UI_COMMAND_SOURCES})
source_group("UI/Presentation" FILES ${FF_UI_PRESENTATION_SOURCES})
source_group("UI/Public Seams" FILES ${FF_UI_PUBLIC_HEADERS})

set_target_properties(FastFiles PROPERTIES FOLDER "App/UI")

target_include_directories(FastFiles PRIVATE include)

# The explicit RT_MANIFEST resource below carries Common Controls v6,
# PerMonitorV2, and longPathAware declarations.  Disable MSVC's automatic
# second manifest resource so the linker does not emit a duplicate RT_MANIFEST.
target_link_options(FastFiles PRIVATE "/MANIFEST:NO")
# Debug HandleMessage + in-process UIA host queries nest a very large
# unoptimized frame; the default 1 MiB stack overflows (STATUS_STACK_OVERFLOW)
# a few seconds after launch when a UIA client attaches. 8 MiB matches the
# headroom used by other Direct2D shells without changing frame layout.
target_link_options(FastFiles PRIVATE "/STACK:8388608")

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
  uiautomationcore
)
