add_executable(fftreemap_layout_tests test_treemap_layout.cpp)
target_include_directories(fftreemap_layout_tests PRIVATE ../../src/ui/include)
add_test(NAME fftreemap_layout_tests COMMAND fftreemap_layout_tests)

add_executable(fftreemap_filter_tests test_treemap_filter.cpp ../../src/ui/src/CategoryEngine.cpp)
target_include_directories(fftreemap_filter_tests PRIVATE ../../src/ui/src ../../src/ui/include)
add_test(NAME fftreemap_filter_tests COMMAND fftreemap_filter_tests)

# recovery-degraded-browsing 7.1: deterministic engine executable discovery
# (candidate order, dedupe, regular-file validation) -- EngineDiscovery.cpp
# is header-only-dependency, so it compiles standalone for the test target.
add_executable(ffengine_discovery_tests test_engine_discovery.cpp ../../src/ui/src/EngineDiscovery.cpp)
target_include_directories(ffengine_discovery_tests PRIVATE ../../src/ui/src ../../src/setup/include)
target_link_libraries(ffengine_discovery_tests PRIVATE ffsetup)
if(FASTFILES_DIAGNOSTIC_ALLOW_UNSIGNED)
  target_compile_definitions(ffengine_discovery_tests PRIVATE FASTFILES_DIAGNOSTIC_ALLOW_UNSIGNED)
endif()
add_test(NAME ffengine_discovery_tests COMMAND ffengine_discovery_tests)

# recovery-degraded-browsing 3.1: keep all status-chip state text and the
# privileged/degraded distinction covered without requiring a renderer.
add_executable(ffengine_status_tests test_engine_status.cpp)
target_include_directories(ffengine_status_tests PRIVATE ../../src/ui/src ../../src/protocol/include)
add_test(NAME ffengine_status_tests COMMAND ffengine_status_tests)

add_executable(ffui_text_encoding_tests test_ui_text_encoding.cpp)
add_test(NAME ffui_text_encoding_tests COMMAND ffui_text_encoding_tests)

add_executable(ffstorage_percentage_tests test_storage_percentage.cpp)
target_include_directories(ffstorage_percentage_tests PRIVATE ../../src/ui/include)
add_test(NAME ffstorage_percentage_tests COMMAND ffstorage_percentage_tests)

add_executable(ffdetails_layout_tests test_details_layout.cpp)
target_include_directories(ffdetails_layout_tests PRIVATE ../../src/ui/include)
add_test(NAME ffdetails_layout_tests COMMAND ffdetails_layout_tests)

add_executable(ffshell_layout_tests test_shell_layout.cpp)
target_include_directories(ffshell_layout_tests PRIVATE ../../src/ui/include)
add_test(NAME ffshell_layout_tests COMMAND ffshell_layout_tests)

# quick-access-home 3.1: pure headless layout seam for the Home surface.
# Mirrors ffshell_layout_tests / ffdetails_layout_tests: include only
# ../../src/ui/include (no src/ui/src) so the header stays renderer-free.
add_executable(ffhome_layout_tests test_home_layout.cpp)
target_include_directories(ffhome_layout_tests PRIVATE ../../src/ui/include)
add_test(NAME ffhome_layout_tests COMMAND ffhome_layout_tests)

add_executable(ffcolumn_hit_test_tests test_column_hit_test.cpp)
target_include_directories(ffcolumn_hit_test_tests PRIVATE
    ../../src/ui/src ../../src/ui/include ../../src/protocol/include
    ../../src/ipc/include ../../src/setup/include ../../src/search/include)
add_test(NAME ffcolumn_hit_test_tests COMMAND ffcolumn_hit_test_tests)

# quick-access-home 4.6: headless tests for the Home hit-test / focus-movement
# pure seams and the layout consumption. Mirrors ffcolumn_hit_test_tests: it
# includes ColumnView.h for the inline geometry seams (no renderer/engine/shell),
# so the same include dirs and no extra link libraries are needed.
add_executable(ffhome_interaction_tests test_home_interaction.cpp)
target_include_directories(ffhome_interaction_tests PRIVATE
    .. ../../src/ui/src ../../src/ui/include ../../src/protocol/include
    ../../src/ipc/include ../../src/setup/include ../../src/search/include)
add_test(NAME ffhome_interaction_tests COMMAND ffhome_interaction_tests)

add_executable(ffsnapshot_aggregate_tests test_snapshot_aggregate.cpp)
target_include_directories(ffsnapshot_aggregate_tests PRIVATE ../../src/ui/include ../../src/protocol/include)
add_test(NAME ffsnapshot_aggregate_tests COMMAND ffsnapshot_aggregate_tests)

add_executable(ffdetails_aggregate_state_tests test_details_aggregate_state.cpp)
target_include_directories(ffdetails_aggregate_state_tests PRIVATE ../../src/ui/include)
add_test(NAME ffdetails_aggregate_state_tests COMMAND ffdetails_aggregate_state_tests)

add_executable(ffselection_identity_tests test_selection_identity.cpp)
target_include_directories(ffselection_identity_tests PRIVATE ../../src/ui/include)
add_test(NAME ffselection_identity_tests COMMAND ffselection_identity_tests)

add_executable(ffsnapshot_freshness_tests test_snapshot_freshness.cpp)
target_include_directories(ffsnapshot_freshness_tests PRIVATE ../../src/ui/include ../../src/protocol/include)
add_test(NAME ffsnapshot_freshness_tests COMMAND ffsnapshot_freshness_tests)

add_executable(ffstorage_visibility_tests test_storage_visibility.cpp)
target_include_directories(ffstorage_visibility_tests PRIVATE ../../src/ui/include)
add_test(NAME ffstorage_visibility_tests COMMAND ffstorage_visibility_tests)

add_executable(ffshortcut_routing_tests test_shortcut_routing.cpp ../../src/ui/src/CommandSystem.cpp)
target_include_directories(ffshortcut_routing_tests PRIVATE ../../src/ui/src)
target_link_libraries(ffshortcut_routing_tests PRIVATE shell32)
add_test(NAME ffshortcut_routing_tests COMMAND ffshortcut_routing_tests)

# semantic-uia-core-surfaces 6.x: headless provider tests over a fake
# UiaSurface (no window/renderer/engine). The provider sources compile into
# the test targets; the seam headers come from src/ui/src + include.
set(FFUIA_PROVIDER_SOURCES
    ../../src/ui/src/UiaProviderBase.cpp
    ../../src/ui/src/ColumnViewUiaProvider.cpp
    ../../src/ui/src/SearchUiaProvider.cpp)
set(FFUIA_TEST_INCLUDES
    .. ../../src/ui/src ../../src/ui/include ../../src/protocol/include
    ../../src/ipc/include ../../src/setup/include ../../src/search/include)
set(FFUIA_TEST_LIBS ole32 oleaut32 uiautomationcore)

add_executable(ffcolumnview_uia_tests test_columnview_uia.cpp ${FFUIA_PROVIDER_SOURCES})
target_include_directories(ffcolumnview_uia_tests PRIVATE ${FFUIA_TEST_INCLUDES})
target_link_libraries(ffcolumnview_uia_tests PRIVATE ${FFUIA_TEST_LIBS})
add_test(NAME ffcolumnview_uia_tests COMMAND ffcolumnview_uia_tests)

add_executable(ffhome_uia_tests test_home_uia.cpp ${FFUIA_PROVIDER_SOURCES})
target_include_directories(ffhome_uia_tests PRIVATE ${FFUIA_TEST_INCLUDES})
target_link_libraries(ffhome_uia_tests PRIVATE ${FFUIA_TEST_LIBS})
add_test(NAME ffhome_uia_tests COMMAND ffhome_uia_tests)

add_executable(ffsearch_uia_tests test_search_uia.cpp ${FFUIA_PROVIDER_SOURCES})
target_include_directories(ffsearch_uia_tests PRIVATE ${FFUIA_TEST_INCLUDES})
target_link_libraries(ffsearch_uia_tests PRIVATE ${FFUIA_TEST_LIBS})
add_test(NAME ffsearch_uia_tests COMMAND ffsearch_uia_tests)

# stabilize-live-uia-e2e: drop-destination pane mapping (pane-of-drop-point).
add_executable(ffdrop_destination_tests test_drop_destination.cpp ../../src/ui/src/OleDragDrop.cpp)
target_include_directories(ffdrop_destination_tests PRIVATE ../../src/ui/include ../../src/ui/src)
target_link_libraries(ffdrop_destination_tests PRIVATE ole32 uuid)
add_test(NAME ffdrop_destination_tests COMMAND ffdrop_destination_tests)
set_tests_properties(ffdrop_destination_tests PROPERTIES TIMEOUT 120)

add_executable(ffuia_geometry_tests test_uia_geometry.cpp)
target_include_directories(ffuia_geometry_tests PRIVATE ../../src/ui/src ../../src/ui/include)
add_test(NAME ffuia_geometry_tests COMMAND ffuia_geometry_tests)

add_executable(ffuia_events_tests test_uia_events.cpp ${FFUIA_PROVIDER_SOURCES})
target_include_directories(ffuia_events_tests PRIVATE ${FFUIA_TEST_INCLUDES})
target_link_libraries(ffuia_events_tests PRIVATE ${FFUIA_TEST_LIBS})
add_test(NAME ffuia_events_tests COMMAND ffuia_events_tests)

# Debug-only read-only state query used by the disposable watcher/reorder
# acceptance; it is intentionally not registered as a CTest test.
add_executable(ffselection_identity_runtime_probe selection_identity_runtime_probe.cpp)
target_include_directories(ffselection_identity_runtime_probe PRIVATE ../../src/ui/include)

# Modernize-ui-appearance foundation seams: token/color conversions, animation
# easing, and icon-cache keying are plain C++ testable without a renderer.
# UiAnimation.cpp is compiled in for FloatAnimation/SystemAnimationsEnabled.
add_executable(ffui_style_tests test_ui_style.cpp ../../src/ui/src/UiAnimation.cpp)
target_include_directories(ffui_style_tests PRIVATE ../../src/ui/include ../../src/ui/src)
target_link_libraries(ffui_style_tests PRIVATE d2d1 dwrite windowscodecs)
add_test(NAME ffui_style_tests COMMAND ffui_style_tests)
set_tests_properties(fftreemap_layout_tests fftreemap_filter_tests ffui_style_tests ffstorage_percentage_tests ffdetails_layout_tests ffshell_layout_tests ffhome_layout_tests ffcolumn_hit_test_tests ffhome_interaction_tests ffsnapshot_aggregate_tests ffdetails_aggregate_state_tests ffselection_identity_tests ffsnapshot_freshness_tests ffstorage_visibility_tests ffshortcut_routing_tests ffengine_status_tests ffui_text_encoding_tests PROPERTIES TIMEOUT 120)
