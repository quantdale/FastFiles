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

# ui-overhaul phase 1: pure responsive policy for the top navigation chrome
# (width class, command family geometry, tab overflow window). Header-only,
# no renderer dependency -- mirrors ffdetails_layout_tests.
add_executable(ffchrome_layout_tests test_chrome_layout.cpp)
target_include_directories(ffchrome_layout_tests PRIVATE ../../src/ui/include)
add_test(NAME ffchrome_layout_tests COMMAND ffchrome_layout_tests)

# ui-overhaul phase 11: the one shared semantic state->color resolver consumed
# by browse rows, the sidebar, and the search list. Header-only (theme tokens +
# system colors); links user32 for GetSysColor and d2d1 for the color types.
add_executable(ffui_state_palette_tests test_ui_state_palette.cpp)
target_include_directories(ffui_state_palette_tests PRIVATE ../../src/ui/src ../../src/ui/include)
target_link_libraries(ffui_state_palette_tests PRIVATE user32 d2d1)
add_test(NAME ffui_state_palette_tests COMMAND ffui_state_palette_tests)

add_executable(ffshell_layout_tests test_shell_layout.cpp)
target_include_directories(ffshell_layout_tests PRIVATE ../../src/ui/include)
add_test(NAME ffshell_layout_tests COMMAND ffshell_layout_tests)

# quick-access-home 3.1: pure headless layout seam for the Home surface.
# Mirrors ffshell_layout_tests / ffdetails_layout_tests: include only
# ../../src/ui/include (no src/ui/src) so the header stays renderer-free.
add_executable(ffhome_layout_tests test_home_layout.cpp)
target_include_directories(ffhome_layout_tests PRIVATE ../../src/ui/include)
add_test(NAME ffhome_layout_tests COMMAND ffhome_layout_tests)

add_executable(ffsidebar_destinations_tests test_sidebar_destinations.cpp)
target_include_directories(ffsidebar_destinations_tests PRIVATE ../../src/ui/include)
add_test(NAME ffsidebar_destinations_tests COMMAND ffsidebar_destinations_tests)

add_executable(ffsettings_pages_tests test_settings_pages.cpp)
target_include_directories(ffsettings_pages_tests PRIVATE ../../src/ui/include)
add_test(NAME ffsettings_pages_tests COMMAND ffsettings_pages_tests)

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

# harden-selection-model-semantics 2.1: direct, executable regression matrix for
# the production selection state machine. Includes the real SelectionModel.h
# (no copy) and asserts membership/anchor/focus after every canonical
# plain/Ctrl/Shift/Ctrl+A transition.
add_executable(ffselection_model_tests test_selection_model.cpp)
target_include_directories(ffselection_model_tests PRIVATE ../../src/ui/src ../../src/ui/include)
add_test(NAME ffselection_model_tests COMMAND ffselection_model_tests)
set_tests_properties(ffselection_model_tests PROPERTIES TIMEOUT 120)

add_executable(ffsnapshot_freshness_tests test_snapshot_freshness.cpp)
target_include_directories(ffsnapshot_freshness_tests PRIVATE ../../src/ui/include ../../src/protocol/include)
add_test(NAME ffsnapshot_freshness_tests COMMAND ffsnapshot_freshness_tests)

# active-path-runtime-resilience 4.1: pure request/source identity gate
# coverage, including a deterministic 150-transition supersession sequence.
add_executable(ffasync_result_gate_tests test_async_result_gate.cpp)
target_include_directories(ffasync_result_gate_tests PRIVATE ../../src/ui/include)
add_test(NAME ffasync_result_gate_tests COMMAND ffasync_result_gate_tests)

# active-path-runtime-resilience 2.5: pure icon-cache recency regression
# coverage for promotion, bounded eviction, and cache clear/reuse.
add_executable(ffui_icon_lru_tests test_icon_lru.cpp)
target_include_directories(ffui_icon_lru_tests PRIVATE ../../src/ui/include)
add_test(NAME ffui_icon_lru_tests COMMAND ffui_icon_lru_tests)

add_executable(ffstorage_ranking_tests test_storage_ranking.cpp)
target_include_directories(ffstorage_ranking_tests PRIVATE ../../src/ui/include ../../src/protocol/include)
target_link_libraries(ffstorage_ranking_tests PRIVATE ffsearch)
add_test(NAME ffstorage_ranking_tests COMMAND ffstorage_ranking_tests)
set_tests_properties(ffstorage_ranking_tests PROPERTIES TIMEOUT 120)

add_executable(ffstorage_visibility_tests test_storage_visibility.cpp)
target_include_directories(ffstorage_visibility_tests PRIVATE ../../src/ui/include)
add_test(NAME ffstorage_visibility_tests COMMAND ffstorage_visibility_tests)

# ui-v3 storage toolbar: the owner-drawn radio presentation keeps checked,
# pressed, focus, and disabled precedence deterministic and headless-testable.
add_executable(ffstorage_radio_state_tests test_storage_radio_state.cpp)
target_include_directories(ffstorage_radio_state_tests PRIVATE ../../src/ui/include)
add_test(NAME ffstorage_radio_state_tests COMMAND ffstorage_radio_state_tests)

# ui-v3 navigation: display-only 8.3/long-path normalization never changes
# canonical activation or identity paths.
add_executable(ffdisplay_path_tests test_display_path.cpp)
target_include_directories(ffdisplay_path_tests PRIVATE ../../src/ui/src ../../src/ui/include)
add_test(NAME ffdisplay_path_tests COMMAND ffdisplay_path_tests)

# keyboard-focus-forensics FF-VIS-FOCUS-001: address-bar close focus-target
# seam (header-only, real-HWND contract test; renderer-free).
add_executable(ffaddress_bar_focus_tests test_address_bar_focus.cpp)
target_include_directories(ffaddress_bar_focus_tests PRIVATE ../../src/ui/include)
target_link_libraries(ffaddress_bar_focus_tests PRIVATE user32)
add_test(NAME ffaddress_bar_focus_tests COMMAND ffaddress_bar_focus_tests)

# retry-ownership-and-focus-escape-closure D4:
# navigation-chrome Tab traversal seam (header-only, real-HWND contract test;
# renderer-free) -- entry, adjacency, both escape boundaries, filtered
# disabled/hidden stops, and address-edit eligibility.
add_executable(ffchrome_tab_traversal_tests test_chrome_tab_traversal.cpp)
target_include_directories(ffchrome_tab_traversal_tests PRIVATE ../../src/ui/include)
target_link_libraries(ffchrome_tab_traversal_tests PRIVATE user32)
add_test(NAME ffchrome_tab_traversal_tests COMMAND ffchrome_tab_traversal_tests)

# modal-close-focus: dialog/modal close focus-target seam (header-only,
# real-HWND contract test; renderer-free).
add_executable(ffdialog_focus_return_tests test_dialog_focus_return.cpp)
target_include_directories(ffdialog_focus_return_tests PRIVATE ../../src/ui/include)
target_link_libraries(ffdialog_focus_return_tests PRIVATE user32)
add_test(NAME ffdialog_focus_return_tests COMMAND ffdialog_focus_return_tests)

# final-local-certification-closure FC-001: command-palette focus readiness
# seam (header-only, real-HWND handle-identity contract test; no foreground
# focus manipulation, non-interactive safe).
add_executable(ffpalette_focus_tests test_palette_focus.cpp)
target_include_directories(ffpalette_focus_tests PRIVATE ../../src/ui/include)
target_link_libraries(ffpalette_focus_tests PRIVATE user32)
add_test(NAME ffpalette_focus_tests COMMAND ffpalette_focus_tests)

add_executable(ffshortcut_routing_tests test_shortcut_routing.cpp ../../src/ui/src/CommandSystem.cpp)
target_include_directories(ffshortcut_routing_tests PRIVATE ../../src/ui/src)
target_link_libraries(ffshortcut_routing_tests PRIVATE shell32)
add_test(NAME ffshortcut_routing_tests COMMAND ffshortcut_routing_tests)

# completion-integrity-hardening-and-release-certification: a clipboard
# manager may strip Preferred DropEffect while preserving CF_HDROP. Keep the
# app-local cut fallback bound to the exact publication and path list.
add_executable(ffquick_actions_tests test_quick_actions.cpp ../../src/ui/src/QuickActions.cpp)
target_include_directories(ffquick_actions_tests PRIVATE ../../src/ui/src)
target_link_libraries(ffquick_actions_tests PRIVATE shell32 ole32)
add_test(NAME ffquick_actions_tests COMMAND ffquick_actions_tests)

# semantic-uia-core-surfaces 6.x: headless provider tests over a fake
# UiaSurface (no window/renderer/engine). The provider sources compile into
# the test targets; the seam headers come from src/ui/src + include.
set(FFUIA_PROVIDER_SOURCES
    ../../src/ui/src/UiaProviderBase.cpp
    ../../src/ui/src/UiaHostQuery.cpp
    ../../src/ui/src/ColumnViewUiaProvider.cpp
    ../../src/ui/src/SearchUiaProvider.cpp
    ../../src/ui/src/SearchButtonUiaProvider.cpp
    ../../src/ui/src/TreemapUiaProvider.cpp
    ../../src/protocol/src/Diagnostics.cpp)
set(FFUIA_TEST_INCLUDES
    .. ../../src/ui/src ../../src/ui/include ../../src/protocol/include
    ../../src/ipc/include ../../src/setup/include ../../src/search/include)
set(FFUIA_TEST_LIBS ole32 oleaut32 uiautomationcore user32)

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

# ui-v3 drag/drop: headless geometry seam coverage for the canonical
# drop-target feedback (dual-pane split strip vs single-pane outline vs
# suppressed states). Mirrors ffhome_layout_tests: header-only, renderer-free.
add_executable(ffdrop_feedback_tests test_drop_feedback.cpp)
target_include_directories(ffdrop_feedback_tests PRIVATE ../../src/ui/include)
add_test(NAME ffdrop_feedback_tests COMMAND ffdrop_feedback_tests)

# completion-integrity-hardening-and-release-certification 4.5.2: Filters
# disclosure AutomationId / expand-collapse mapping, header-only.
add_executable(ffsearch_filters_disclosure_tests test_search_filters_disclosure.cpp)
target_include_directories(ffsearch_filters_disclosure_tests PRIVATE ../../src/ui/include)
add_test(NAME ffsearch_filters_disclosure_tests COMMAND ffsearch_filters_disclosure_tests)

# completion-integrity-hardening-and-release-certification 4.5.2: Search
# snapshot-refresh coalescing rule, header-only. Pins that a generation
# arriving inside the rate-limit interval still schedules a refresh.
add_executable(ffsearch_snapshot_refresh_tests test_search_snapshot_refresh.cpp)
target_include_directories(ffsearch_snapshot_refresh_tests PRIVATE ../../src/ui/include)
add_test(NAME ffsearch_snapshot_refresh_tests COMMAND ffsearch_snapshot_refresh_tests)

add_executable(ffgui_show_command_tests test_gui_show_command.cpp)
target_include_directories(ffgui_show_command_tests PRIVATE ../../src/ui/include)
add_test(NAME ffgui_show_command_tests COMMAND ffgui_show_command_tests)

# ui-v3 search a11y hardening: Search panel keyboard focus-order seam.
# Header-only, renderer-free; pins the collapsed/expanded Tab and Shift+Tab
# cycles, wrap-around edges, the always-present Filters disclosure, and
# stale-target clamping after collapse.
add_executable(ffsearch_tab_order_tests test_search_tab_order.cpp)
target_include_directories(ffsearch_tab_order_tests PRIVATE ../../src/ui/include)
add_test(NAME ffsearch_tab_order_tests COMMAND ffsearch_tab_order_tests)

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

# semantic-treemap-uia 33.J: pure headless metadata/formatting tests.
add_executable(fftreemap_metadata_tests test_treemap_metadata.cpp)
# EX-009: TreemapSemantics.h now shares the snapshot-aggregate helper, so this
# target needs the protocol headers too (mirrors ffsnapshot_aggregate_tests).
target_include_directories(fftreemap_metadata_tests PRIVATE ../../src/ui/include ../../src/protocol/include)
add_test(NAME fftreemap_metadata_tests COMMAND fftreemap_metadata_tests)

# semantic-treemap-uia 33.M: headless provider tests over a fake treemap surface.
add_executable(fftreemap_uia_tests test_treemap_uia.cpp ${FFUIA_PROVIDER_SOURCES})
target_include_directories(fftreemap_uia_tests PRIVATE ${FFUIA_TEST_INCLUDES})
target_link_libraries(fftreemap_uia_tests PRIVATE ${FFUIA_TEST_LIBS})
add_test(NAME fftreemap_uia_tests COMMAND fftreemap_uia_tests)

# Modernize-ui-appearance foundation seams: token/color conversions, animation
# easing, and icon-cache keying are plain C++ testable without a renderer.
# UiAnimation.cpp is compiled in for FloatAnimation/SystemAnimationsEnabled.
add_executable(ffui_style_tests test_ui_style.cpp ../../src/ui/src/UiAnimation.cpp)
target_include_directories(ffui_style_tests PRIVATE ../../src/ui/include ../../src/ui/src)
target_link_libraries(ffui_style_tests PRIVATE d2d1 dwrite windowscodecs)
add_test(NAME ffui_style_tests COMMAND ffui_style_tests)

add_executable(ffnative_header_theme_tests test_native_header_theme.cpp)
target_include_directories(ffnative_header_theme_tests PRIVATE .. ../../src/ui/src ../../src/ui/include)
target_link_libraries(ffnative_header_theme_tests PRIVATE comctl32 uxtheme user32)
add_test(NAME ffnative_header_theme_tests COMMAND ffnative_header_theme_tests)

# indexed-search-allocated-size D7: pure headless test for the shared Size-on-Disk
# wording helper (no renderer/UI deps, mirrors fftreemap_metadata_tests).
add_executable(ffsearch_metadata_tests test_search_metadata.cpp)
target_include_directories(ffsearch_metadata_tests PRIVATE ../../src/ui/include)
add_test(NAME ffsearch_metadata_tests COMMAND ffsearch_metadata_tests)

# Visual intelligence autopilot: deterministic component gallery lifecycle,
# gating, section enumeration, geometry, and hit-testing regression tests.
add_executable(ffgallery_tests test_gallery.cpp ../../src/ui/src/Gallery.cpp)
target_include_directories(ffgallery_tests PRIVATE ../../src/ui/src ../../src/ui/include)
target_link_libraries(ffgallery_tests PRIVATE d2d1 dwrite shell32 ole32 user32)
add_test(NAME ffgallery_tests COMMAND ffgallery_tests)
set_tests_properties(ffgallery_tests PROPERTIES TIMEOUT 120)
set_tests_properties(fftreemap_layout_tests fftreemap_filter_tests fftreemap_metadata_tests fftreemap_uia_tests ffui_style_tests ffui_icon_lru_tests ffstorage_percentage_tests ffdetails_layout_tests ffshell_layout_tests ffhome_layout_tests ffcolumn_hit_test_tests ffhome_interaction_tests ffsnapshot_aggregate_tests ffdetails_aggregate_state_tests ffselection_identity_tests ffsnapshot_freshness_tests ffasync_result_gate_tests ffstorage_visibility_tests ffstorage_radio_state_tests ffdisplay_path_tests ffshortcut_routing_tests ffaddress_bar_focus_tests ffdialog_focus_return_tests ffpalette_focus_tests ffquick_actions_tests ffengine_status_tests ffui_text_encoding_tests ffsearch_metadata_tests ffdrop_feedback_tests ffsearch_tab_order_tests ffsearch_filters_disclosure_tests ffsearch_snapshot_refresh_tests ffgui_show_command_tests ffnative_header_theme_tests PROPERTIES TIMEOUT 120)
set_tests_properties(ffengine_discovery_tests ffcolumnview_uia_tests ffhome_uia_tests ffsearch_uia_tests ffuia_geometry_tests ffuia_events_tests PROPERTIES TIMEOUT 120)

add_executable(ffowned_message_drain_tests test_owned_message_drain.cpp)
target_include_directories(ffowned_message_drain_tests PRIVATE ../../src/ui/src)
target_link_libraries(ffowned_message_drain_tests PRIVATE user32)
add_test(NAME ffowned_message_drain_tests COMMAND ffowned_message_drain_tests)
set_tests_properties(ffowned_message_drain_tests PROPERTIES TIMEOUT 15)
