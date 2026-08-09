add_executable(fftreemap_layout_tests test_treemap_layout.cpp)
target_include_directories(fftreemap_layout_tests PRIVATE ../../src/ui/include)
add_test(NAME fftreemap_layout_tests COMMAND fftreemap_layout_tests)

# recovery-degraded-browsing 7.1: deterministic engine executable discovery
# (candidate order, dedupe, regular-file validation) -- EngineDiscovery.cpp
# is header-only-dependency, so it compiles standalone for the test target.
add_executable(ffengine_discovery_tests test_engine_discovery.cpp ../../src/ui/src/EngineDiscovery.cpp)
target_include_directories(ffengine_discovery_tests PRIVATE ../../src/ui/src ../../src/setup/include)
add_test(NAME ffengine_discovery_tests COMMAND ffengine_discovery_tests)

add_executable(ffstorage_percentage_tests test_storage_percentage.cpp)
target_include_directories(ffstorage_percentage_tests PRIVATE ../../src/ui/include)
add_test(NAME ffstorage_percentage_tests COMMAND ffstorage_percentage_tests)

add_executable(ffdetails_layout_tests test_details_layout.cpp)
target_include_directories(ffdetails_layout_tests PRIVATE ../../src/ui/include)
add_test(NAME ffdetails_layout_tests COMMAND ffdetails_layout_tests)

add_executable(ffcolumn_hit_test_tests test_column_hit_test.cpp)
target_include_directories(ffcolumn_hit_test_tests PRIVATE
    ../../src/ui/src ../../src/ui/include ../../src/protocol/include
    ../../src/ipc/include ../../src/setup/include ../../src/search/include)
add_test(NAME ffcolumn_hit_test_tests COMMAND ffcolumn_hit_test_tests)

add_executable(ffsnapshot_aggregate_tests test_snapshot_aggregate.cpp)
target_include_directories(ffsnapshot_aggregate_tests PRIVATE ../../src/ui/include ../../src/protocol/include)
add_test(NAME ffsnapshot_aggregate_tests COMMAND ffsnapshot_aggregate_tests)

add_executable(ffselection_identity_tests test_selection_identity.cpp)
target_include_directories(ffselection_identity_tests PRIVATE ../../src/ui/include)
add_test(NAME ffselection_identity_tests COMMAND ffselection_identity_tests)

add_executable(ffstorage_visibility_tests test_storage_visibility.cpp)
target_include_directories(ffstorage_visibility_tests PRIVATE ../../src/ui/include)
add_test(NAME ffstorage_visibility_tests COMMAND ffstorage_visibility_tests)

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
set_tests_properties(fftreemap_layout_tests ffui_style_tests ffstorage_percentage_tests ffdetails_layout_tests ffcolumn_hit_test_tests ffsnapshot_aggregate_tests ffselection_identity_tests ffstorage_visibility_tests PROPERTIES TIMEOUT 120)
