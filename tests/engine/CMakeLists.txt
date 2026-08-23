add_executable(ffengine_index_pipeline_tests
  test_index_pipeline.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/src/IndexPipeline.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/src/VolumeIdentity.cpp
)
target_include_directories(ffengine_index_pipeline_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/engine/src)
target_link_libraries(ffengine_index_pipeline_tests PRIVATE ffindexstore ffprotocol ffsearch Rpcrt4 Shell32 Ole32)

add_test(NAME ffengine_index_pipeline_tests COMMAND ffengine_index_pipeline_tests)

add_executable(ffengine_volume_session_manager_tests
  test_volume_session_manager.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/src/IndexPipeline.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/src/PrivilegedConnection.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/src/VolumeIdentity.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/src/VolumeSessionManager.cpp
)
target_include_directories(ffengine_volume_session_manager_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/engine/src)
target_link_libraries(ffengine_volume_session_manager_tests PRIVATE ffindexstore ffprotocol ffsearch ffipc ffsetup Rpcrt4 Shell32 Ole32)
add_test(NAME ffengine_volume_session_manager_tests COMMAND ffengine_volume_session_manager_tests)

# Portable pin-enforcement suite: no elevation, no production signer/pins, no
# trust-store mutation. This is the mandatory default; it must keep passing on
# an ordinary non-admin Windows host and in hosted CI.
add_executable(ffengine_authenticode_verification_tests
  test_authenticode_verification.cpp
)
target_include_directories(ffengine_authenticode_verification_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/engine/src)
target_link_libraries(ffengine_authenticode_verification_tests PRIVATE ffsetup)
add_custom_target(ffengine_authenticode_fixture_script ALL
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/AuthenticodeFixture.ps1"
    "$<TARGET_FILE_DIR:ffengine_authenticode_verification_tests>/AuthenticodeFixture.ps1"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/AuthenticodeFixture.ps1"
)
add_dependencies(ffengine_authenticode_verification_tests ffengine_authenticode_fixture_script)
add_test(NAME ffengine_authenticode_verification_tests COMMAND ffengine_authenticode_verification_tests)

# Privileged CurrentUser\Root trust-store integration suite: opt-in only, not
# part of the default CTest run. See AGENTS.md "Normal FastFiles development"
# and verify/run_privileged_trust_tests.ps1. The option is declared once in
# the top-level CMakeLists.txt.
if(FASTFILES_ENABLE_PRIVILEGED_TRUST_TESTS)
  add_executable(ffengine_authenticode_wintrust_integration_tests
    test_authenticode_wintrust_integration.cpp
  )
  target_include_directories(ffengine_authenticode_wintrust_integration_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/engine/src)
  target_link_libraries(ffengine_authenticode_wintrust_integration_tests PRIVATE ffsetup)
  add_custom_target(ffengine_authenticode_wintrust_fixture_script ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/AuthenticodeFixture.ps1"
      "$<TARGET_FILE_DIR:ffengine_authenticode_wintrust_integration_tests>/AuthenticodeFixture.ps1"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/AuthenticodeFixture.ps1"
  )
  add_dependencies(ffengine_authenticode_wintrust_integration_tests ffengine_authenticode_wintrust_fixture_script)
  add_test(NAME ffengine_authenticode_wintrust_integration_tests
    COMMAND ffengine_authenticode_wintrust_integration_tests)
  add_test(NAME ffengine_authenticode_wintrust_integration_timestamp_tests
    COMMAND ffengine_authenticode_wintrust_integration_tests --timestamped)
  set_tests_properties(ffengine_authenticode_wintrust_integration_tests
    ffengine_authenticode_wintrust_integration_timestamp_tests
    PROPERTIES TIMEOUT 600 LABELS privileged-trust)
endif()

add_executable(ffengine_degraded_special_files_tests
  test_degraded_special_files.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/src/DegradedModeEnumerator.cpp
)
target_include_directories(ffengine_degraded_special_files_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/engine/src)
target_link_libraries(ffengine_degraded_special_files_tests PRIVATE shell32)
add_test(NAME ffengine_degraded_special_files_tests COMMAND ffengine_degraded_special_files_tests)

add_executable(ffengine_directory_watcher_tests
  test_directory_watcher.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/src/DirectoryWatcher.cpp
)
target_include_directories(ffengine_directory_watcher_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/engine/src)
target_link_libraries(ffengine_directory_watcher_tests PRIVATE ffprotocol)
target_compile_definitions(ffengine_directory_watcher_tests PRIVATE FF_DIRECTORY_WATCHER_TESTING=1)
add_test(NAME ffengine_directory_watcher_tests COMMAND ffengine_directory_watcher_tests)

add_executable(ffengine_ui_server_lifecycle_tests
  test_ui_server_lifecycle.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/src/UiServer.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/src/DirectoryWatcher.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/src/SnapshotPublisher.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/src/DegradedModeEnumerator.cpp
)
target_include_directories(ffengine_ui_server_lifecycle_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/engine/src)
target_link_libraries(ffengine_ui_server_lifecycle_tests PRIVATE ffprotocol ffipc ffsetup ffindexstore shell32)
# fix-initial-snapshot-handshake: enables the initial-publication
# failure-injection seams (see SnapshotPublisher.h) for fail-closed coverage.
target_compile_definitions(ffengine_ui_server_lifecycle_tests PRIVATE FF_SNAPSHOT_PUBLISHER_TESTING=1)
add_test(NAME ffengine_ui_server_lifecycle_tests COMMAND ffengine_ui_server_lifecycle_tests)

add_executable(ffengine_snapshot_publisher_tests
  test_snapshot_publisher.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/src/SnapshotPublisher.cpp
)
target_include_directories(ffengine_snapshot_publisher_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/engine/src)
target_link_libraries(ffengine_snapshot_publisher_tests PRIVATE ffprotocol ffsetup)
# fix-initial-snapshot-handshake: enables the initial-publication
# failure-injection seams (see SnapshotPublisher.h) for fail-closed coverage.
target_compile_definitions(ffengine_snapshot_publisher_tests PRIVATE FF_SNAPSHOT_PUBLISHER_TESTING=1)
add_test(NAME ffengine_snapshot_publisher_tests COMMAND ffengine_snapshot_publisher_tests)

# fix-initial-snapshot-handshake (tasks.md 3.x): end-to-end EngineClient <->
# UiServer handshake over the real control pipe and real shared-memory
# snapshots, with no directory published first. Unprivileged, no service, no
# signing, no persistent user index.
add_executable(ffengine_client_handshake_tests
  test_engine_client_handshake.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/src/UiServer.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/src/DirectoryWatcher.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/src/SnapshotPublisher.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/src/DegradedModeEnumerator.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/src/EngineClient.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/src/EngineDiscovery.cpp
)
target_include_directories(ffengine_client_handshake_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/src/engine/src
  ${CMAKE_SOURCE_DIR}/src/ui/src
  ${CMAKE_SOURCE_DIR}/src/ui/include)
target_link_libraries(ffengine_client_handshake_tests PRIVATE ffprotocol ffipc ffsetup ffindexstore shell32)
add_test(NAME ffengine_client_handshake_tests COMMAND ffengine_client_handshake_tests)

# zero-touch-autonomous-engineering: subtree-gating unit tests (closes
# settings-and-appearance 2.5). Compiles IndexPipeline.cpp directly so the
# engine's rule-honoring ingestion is testable without the GUI/service.
add_executable(ffengine_subtree_gating_tests
  test_subtree_gating.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/src/IndexPipeline.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/src/VolumeIdentity.cpp
)
target_include_directories(ffengine_subtree_gating_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/engine/src)
target_link_libraries(ffengine_subtree_gating_tests PRIVATE ffindexstore ffprotocol ffsearch Rpcrt4 Shell32 Ole32)
add_test(NAME ffengine_subtree_gating_tests COMMAND ffengine_subtree_gating_tests)

# memory-bounded-indexed-scale 6.x: deterministic seeded indexed-mode
# acceptance. Populates the real Store/IndexPipeline with synthetic
# MFT/USN-equivalent records and drives population, restart, navigation export,
# search (trigram + canonical + cold), aggregates, mutations, reconciliation,
# unavailable volumes, cancellation, and snapshot publish/parse round-trips.
add_executable(ffengine_seeded_indexed_mode_tests
  test_seeded_indexed_mode.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/src/IndexPipeline.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/src/VolumeIdentity.cpp
)
target_include_directories(ffengine_seeded_indexed_mode_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/engine/src)
target_link_libraries(ffengine_seeded_indexed_mode_tests PRIVATE ffindexstore ffprotocol ffsearch Rpcrt4 Shell32 Ole32)
add_test(NAME ffengine_seeded_indexed_mode_tests COMMAND ffengine_seeded_indexed_mode_tests)

# Informational only: explicit large-index scalability evidence, never a CTest
# wall-clock gate and never run by the normal test suite.
add_executable(ffengine_bench_large_index_scalability
  bench_large_index_scalability.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/src/IndexPipeline.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/src/VolumeIdentity.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/src/SnapshotPublisher.cpp
)
target_include_directories(ffengine_bench_large_index_scalability PRIVATE ${CMAKE_SOURCE_DIR}/src/engine/src)
target_link_libraries(ffengine_bench_large_index_scalability PRIVATE ffindexstore ffprotocol ffsearch ffsetup psapi Rpcrt4 Shell32 Ole32)

# Informational only: compares bounded secondary filename-index candidates
# against the canonical matcher. It is intentionally not registered with
# CTest because its scales are selected for engineering evidence, not a
# wall-clock gate.
add_executable(ffengine_bench_secondary_filename_index
  bench_secondary_filename_index.cpp
)
target_link_libraries(ffengine_bench_secondary_filename_index PRIVATE ffsearch sqlite3 psapi)

set_tests_properties(ffengine_index_pipeline_tests ffengine_volume_session_manager_tests ffengine_degraded_special_files_tests ffengine_directory_watcher_tests ffengine_ui_server_lifecycle_tests ffengine_snapshot_publisher_tests ffengine_subtree_gating_tests ffengine_client_handshake_tests ffengine_seeded_indexed_mode_tests PROPERTIES TIMEOUT 120)
set_tests_properties(ffengine_authenticode_verification_tests PROPERTIES TIMEOUT 600)
