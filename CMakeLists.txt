cmake_minimum_required(VERSION 3.25)
# C is enabled solely to compile the vendored SQLite amalgamation
# (third_party/sqlite); all first-party FastFiles code is C++20.
project(FastFiles LANGUAGES CXX C)

# Keep CMake-aware IDEs navigable without hand-maintained Visual Studio
# project files.  The target folders below are assigned after all component
# directories have been added so optional test targets are handled uniformly.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(NOT WIN32)
  message(FATAL_ERROR "FastFiles targets Windows only (Win32/COM/Direct2D/NTFS APIs).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Windows 10 1809+ / Windows 11 target.
add_compile_definitions(
  _WIN32_WINNT=0x0A00
  WINVER=0x0A00
  UNICODE
  _UNICODE
  NOMINMAX
  WIN32_LEAN_AND_MEAN
)

option(FASTFILES_BUILD_TESTS "Build FastFiles unit tests" ON)
option(FASTFILES_STATIC_ANALYSIS "Enable MSVC /analyze static analysis" OFF)
option(FASTFILES_RELEASE_TEST_BUILD
  "Build an optimized unsigned test-release artifact; never represents a production release" OFF)

# Opt-in only. Registers the CurrentUser\Root WinTrust trust-store
# integration tests (tests/engine, tests/setup) with CTest under the
# "privileged-trust" label. Ordinary Debug/release-test/analyze development
# and hosted CI never need this ON -- see AGENTS.md "Normal FastFiles
# development" and verify/run_privileged_trust_tests.ps1.
option(FASTFILES_ENABLE_PRIVILEGED_TRUST_TESTS
  "Register the privileged CurrentUser\\Root WinTrust integration tests with CTest (opt-in; never required for ordinary development)" OFF)

# Keep the unsigned-authentication escape hatch visibly outside every shipped
# or optimized test-release configuration before any release-pin validation is
# evaluated. The implementation target repeats this guard for local clarity.
if(FASTFILES_DIAGNOSTIC_ALLOW_UNSIGNED AND
   (NOT CMAKE_BUILD_TYPE STREQUAL "Debug" OR FASTFILES_RELEASE_TEST_BUILD))
  message(FATAL_ERROR
    "FASTFILES_DIAGNOSTIC_ALLOW_UNSIGNED may only be enabled in an explicitly "
    "Debug diagnostic build; shipped and optimized release-test builds must "
    "compile the fail-closed Authenticode path.")
endif()

# Authenticode pins are release inputs, not source-controlled product data.
# CI supplies the two certificate thumbprints through environment variables or
# explicit cache entries; development configurations intentionally retain the
# all-zero fail-closed fallback until an explicit diagnostic override is
# selected.  Never silently turn an unprovisioned release into a build that
# can authenticate arbitrary peers.
set(FASTFILES_ENGINE_SIGNATURE_THUMBPRINT "$ENV{FASTFILES_ENGINE_SIGNATURE_THUMBPRINT}"
    CACHE STRING "SHA-1 thumbprint for the signed FastFilesEngine release binary")
set(FASTFILES_INDEX_SVC_SIGNATURE_THUMBPRINT "$ENV{FASTFILES_INDEX_SVC_SIGNATURE_THUMBPRINT}"
    CACHE STRING "SHA-1 thumbprint for the signed FastFilesIndexSvc release binary")

function(fastfiles_normalize_thumbprint variable_name output_variable)
  set(value "${${variable_name}}")
  string(STRIP "${value}" value)
  if(NOT value STREQUAL "")
    string(LENGTH "${value}" value_length)
    if(NOT value_length EQUAL 40)
      message(FATAL_ERROR
        "${variable_name} must contain exactly 40 hexadecimal characters when provisioned.")
    endif()
    string(REGEX MATCH "[^0-9A-Fa-f]" invalid_character "${value}")
    if(NOT invalid_character STREQUAL "")
      message(FATAL_ERROR "${variable_name} contains a non-hexadecimal character.")
    endif()
  endif()

  set(bytes "")
  if(value STREQUAL "")
    set(bytes "0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00")
  else()
    foreach(offset RANGE 0 38 2)
      string(SUBSTRING "${value}" ${offset} 2 byte)
      string(TOUPPER "${byte}" byte)
      if(bytes STREQUAL "")
        set(bytes "0x${byte}")
      else()
        string(APPEND bytes ", 0x${byte}")
      endif()
    endforeach()
  endif()
  set(${output_variable} "${bytes}" PARENT_SCOPE)
endfunction()

fastfiles_normalize_thumbprint(
  FASTFILES_ENGINE_SIGNATURE_THUMBPRINT
  FASTFILES_ENGINE_SIGNATURE_THUMBPRINT_BYTES)
fastfiles_normalize_thumbprint(
  FASTFILES_INDEX_SVC_SIGNATURE_THUMBPRINT
  FASTFILES_INDEX_SVC_SIGNATURE_THUMBPRINT_BYTES)

if(FASTFILES_RELEASE_TEST_BUILD AND NOT CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
  message(FATAL_ERROR
    "FASTFILES_RELEASE_TEST_BUILD requires an optimized CMake build type (Release, RelWithDebInfo, or MinSizeRel).")
endif()

if(CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$" AND NOT FASTFILES_RELEASE_TEST_BUILD)
  if(FASTFILES_ENGINE_SIGNATURE_THUMBPRINT STREQUAL "" OR
     FASTFILES_INDEX_SVC_SIGNATURE_THUMBPRINT STREQUAL "" OR
     FASTFILES_ENGINE_SIGNATURE_THUMBPRINT MATCHES "^0+$" OR
     FASTFILES_INDEX_SVC_SIGNATURE_THUMBPRINT MATCHES "^0+$")
    message(FATAL_ERROR
      "Release FastFiles builds require FASTFILES_ENGINE_SIGNATURE_THUMBPRINT and "
      "FASTFILES_INDEX_SVC_SIGNATURE_THUMBPRINT from the signing pipeline; "
      "refusing to configure with placeholder Authenticode pins.")
  endif()
endif()

if(FASTFILES_RELEASE_TEST_BUILD)
  add_compile_definitions(FASTFILES_RELEASE_TEST_BUILD=1)
  message(STATUS
    "Configuring an optimized unsigned test-release artifact; production signing and pin enforcement remain external.")
endif()

set(FASTFILES_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${FASTFILES_GENERATED_INCLUDE_DIR}/ffsetup")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FastFilesVersion.cmake")
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/src/setup/PinnedSignaturesConfig.h.in"
  "${FASTFILES_GENERATED_INCLUDE_DIR}/ffsetup/PinnedSignaturesConfig.h"
  @ONLY)

# Add the vendored C target before applying first-party warning and analysis
# policy. SQLite keeps its own warning configuration in its subdirectory and
# must not fail FastFiles analysis on Windows SDK annotations.
add_subdirectory(third_party/sqlite)

if(MSVC)
  # First-party source files are UTF-8; keep raw wide UI literals from being
  # decoded through the active legacy code page (which produces mojibake).
  add_compile_options(/W4 /WX /permissive- /sdl /guard:cf /Zc:__cplusplus /utf-8)
  add_link_options(/DYNAMICBASE /NXCOMPAT /guard:cf)
endif()

if(MSVC AND FASTFILES_STATIC_ANALYSIS)
  add_compile_options(/analyze)
endif()

add_subdirectory(src/protocol)
add_subdirectory(src/ipc)
add_subdirectory(src/setup)
add_subdirectory(src/indexstore)
add_subdirectory(src/search)
add_subdirectory(src/indexsvc)
add_subdirectory(src/engine)
add_subdirectory(src/ui)
add_subdirectory(src/installer)
add_subdirectory(src/fftest)

if(FASTFILES_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

function(fastfiles_set_target_folder folder)
  foreach(target IN LISTS ARGN)
    if(TARGET "${target}")
      set_property(TARGET "${target}" PROPERTY FOLDER "${folder}")
    endif()
  endforeach()
endfunction()

# Product topology: the folders reflect the three-process split and the
# shared-library boundaries documented in AGENTS.md.
fastfiles_set_target_folder("App/UI" FastFiles)
fastfiles_set_target_folder("App/Engine" FastFilesEngine)
fastfiles_set_target_folder("App/Service" FastFilesIndexSvc)
fastfiles_set_target_folder("App/Installer" FastFilesSetup)
fastfiles_set_target_folder("Libraries/Protocol" ffprotocol)
fastfiles_set_target_folder("Libraries/IPC" ffipc)
fastfiles_set_target_folder("Libraries/Index Store" ffindexstore)
fastfiles_set_target_folder("Libraries/Search" ffsearch)
fastfiles_set_target_folder("Libraries/Setup" ffsetup)
fastfiles_set_target_folder("Libraries/Service" ffmftparser ffusnparser ffindexsvcprobes)
fastfiles_set_target_folder("Tools/Diagnostics" fftest ffntfsobserve)
fastfiles_set_target_folder("Third Party/SQLite" sqlite3)

# Test topology: keep fast unit/component tests separate from UI automation,
# verification scripts, and optional privileged trust-store tests.  Optional
# targets are ignored by the helper when their feature is disabled.
fastfiles_set_target_folder("Tests/Protocol"
  ffprotocol_tests ffprotocol_diagnostics_tests ffprotocol_diagnostics_concurrency_tests ffprotocol_fuzz_tests)
fastfiles_set_target_folder("Tests/IPC" ffipc_framing_tests ffipc_listener_lifecycle_tests)
fastfiles_set_target_folder("Tests/Index Store"
  ffindexstore_store_tests ffindexstore_projection_tests ffindexstore_flat_hash_map_tests
  ffindexstore_read_session_tests ffindexstore_bench_projection_memory)
fastfiles_set_target_folder("Tests/Search"
  ffsearch_tests ffsearch_ascii_fold_tests ffsearch_scan_tests ffsearch_ondisk_filter_tests
  ffsearch_history_tests ffsearch_bench_literal_matcher)
fastfiles_set_target_folder("Tests/Index Service"
  ffmftparser_tests ffusnrecord_tests ffprivilege_diagnostics_tests ffcommand_surface_tests
  ffconnection_registry_tests ffvolumescanner_terminal_tests ffindexsvc_write_deadline_policy_tests
  ffntfsobserve_tests)
fastfiles_set_target_folder("Tests/Engine"
  ffengine_index_pipeline_tests ffengine_volume_session_manager_tests
  ffengine_authenticode_verification_tests ffengine_authenticode_wintrust_integration_tests
  ffengine_authenticode_wintrust_integration_timestamp_tests ffengine_degraded_special_files_tests
  ffengine_directory_watcher_tests ffengine_idle_manager_tests ffengine_ui_server_lifecycle_tests
  ffengine_snapshot_publisher_tests ffengine_client_handshake_tests ffengine_active_path_resilience_tests
  ffengine_subtree_gating_tests ffengine_seeded_indexed_mode_tests ffengine_index_pipeline_concurrency_tests
  ffengine_bench_large_index_scalability ffengine_bench_secondary_filename_index)
fastfiles_set_target_folder("Tests/Navigation" ffnavigation_tests)
fastfiles_set_target_folder("Tests/Commands" ffcommand_tests)
fastfiles_set_target_folder("Tests/File Operations"
  fffileoperations_tests fffileoperations_e2e_tests ffselection_tests)
fastfiles_set_target_folder("Tests/Preview" ffpreview_tests)
fastfiles_set_target_folder("Tests/UI"
  fftreemap_layout_tests fftreemap_filter_tests ffengine_discovery_tests ffengine_status_tests
  ffui_text_encoding_tests ffstorage_percentage_tests ffdetails_layout_tests ffchrome_layout_tests
  ffui_state_palette_tests ffshell_layout_tests ffhome_layout_tests ffsidebar_destinations_tests
  ffsettings_pages_tests ffcolumn_hit_test_tests ffhome_interaction_tests ffsnapshot_aggregate_tests
  ffdetails_aggregate_state_tests ffselection_identity_tests ffselection_model_tests
  ffsnapshot_freshness_tests ffasync_result_gate_tests ffui_icon_lru_tests ffstorage_visibility_tests
  ffstorage_radio_state_tests ffdisplay_path_tests ffaddress_bar_focus_tests ffdialog_focus_return_tests
  ffpalette_focus_tests ffshortcut_routing_tests ffquick_actions_tests ffcolumnview_uia_tests
  ffhome_uia_tests ffsearch_uia_tests ffdrop_destination_tests ffdrop_feedback_tests
  ffsearch_filters_disclosure_tests ffsearch_snapshot_refresh_tests ffgui_show_command_tests
  ffsearch_tab_order_tests ffuia_geometry_tests ffuia_events_tests ffselection_identity_runtime_probe
  fftreemap_metadata_tests fftreemap_uia_tests ffui_style_tests ffnative_header_theme_tests
  ffsearch_metadata_tests ffgallery_tests)
fastfiles_set_target_folder("Tests/Setup"
  ffsetup_security_descriptor_tests ffsetup_peer_image_match_tests ffsetup_package_trust_tests
  ffsetup_package_trust_wintrust_integration_tests ffsetup_installer_source_policy_tests
  ffsetup_install_lifecycle_tests)
fastfiles_set_target_folder("Tests/Tooling" ffprovider_contract_ps_tests ffproject_status_ps_tests)
fastfiles_set_target_folder("Tests/Verification"
  ffuia_driver_ps_tests ffintake_gate_ps_tests ffverify_result_composition_ps_tests
  ffverify_uia_selection_ps_tests ffverify_process_reaper_ps_tests ffe2e_harness_ps_tests ffdoc_drift_tests
  ffdoc_truth_fixture_tests ffsource_provenance_fixture_tests)

if(CMAKE_GENERATOR MATCHES "Visual Studio")
  set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT FastFiles)
endif()
