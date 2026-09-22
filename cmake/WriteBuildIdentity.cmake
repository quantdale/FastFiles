# Build-time FastFiles source/build identity generator.
#
# Runs on every build (add_custom_target ALL) and rewrites its outputs only
# when the rendered bytes change, so an unchanged identity costs zero
# recompiles while a new commit always refreshes PrivateBuild / kBuildIdentity.
#
# Required -D inputs:
#   SOURCE_DIR          - repository root (git -C target)
#   OUTPUT_HEADER       - path of ffsetup/BuildIdentity.h to write
#   OUTPUT_RC_DEFINES   - path of ffsetup/BuildIdentity.rc.h to write
#   CONFIGURATION       - CMake build type / preset configuration label
#   ARCHITECTURE        - target architecture label (x64/arm64/x86)
# Optional:
#   EXPECTED_BUILD_ID   - explicit identity override (mirrors ENV FASTFILES_BUILD_ID)

cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED SOURCE_DIR OR NOT DEFINED OUTPUT_HEADER OR NOT DEFINED OUTPUT_RC_DEFINES)
  message(FATAL_ERROR "WriteBuildIdentity.cmake requires SOURCE_DIR, OUTPUT_HEADER, and OUTPUT_RC_DEFINES.")
endif()
if(NOT DEFINED CONFIGURATION)
  set(CONFIGURATION "unknown")
endif()
if(NOT DEFINED ARCHITECTURE)
  set(ARCHITECTURE "unknown")
endif()

set(ff_build_id "")
if(DEFINED EXPECTED_BUILD_ID AND NOT EXPECTED_BUILD_ID STREQUAL "")
  set(ff_build_id "${EXPECTED_BUILD_ID}")
elseif(DEFINED ENV{FASTFILES_BUILD_ID} AND NOT "$ENV{FASTFILES_BUILD_ID}" STREQUAL "")
  set(ff_build_id "$ENV{FASTFILES_BUILD_ID}")
else()
  execute_process(
    COMMAND git -C "${SOURCE_DIR}" rev-parse HEAD
    RESULT_VARIABLE ff_git_result
    OUTPUT_VARIABLE ff_build_id
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  if(NOT ff_git_result EQUAL 0)
    set(ff_build_id "")
  endif()
endif()

string(STRIP "${ff_build_id}" ff_build_id)
string(LENGTH "${ff_build_id}" ff_build_id_length)
if(NOT ff_build_id_length EQUAL 40 OR NOT ff_build_id MATCHES "^[0-9a-fA-F]+$")
  message(FATAL_ERROR
    "FASTFILES_BUILD_ID must be a 40-character Git identity (or FASTFILES_BUILD_ID must be set).")
endif()
string(TOLOWER "${ff_build_id}" ff_build_id)

# Dirty/clean is derived from the worktree every build. An explicit identity
# override cannot prove cleanliness, so it is reported as unbound.
set(ff_tree_state "unbound")
set(ff_commit_time "unbound")
if(NOT DEFINED EXPECTED_BUILD_ID OR EXPECTED_BUILD_ID STREQUAL "")
  if(DEFINED ENV{FASTFILES_BUILD_ID} AND NOT "$ENV{FASTFILES_BUILD_ID}" STREQUAL "")
    set(ff_tree_state "unbound")
    set(ff_commit_time "unbound")
  else()
    execute_process(
      COMMAND git -C "${SOURCE_DIR}" status --porcelain
      RESULT_VARIABLE ff_status_result
      OUTPUT_VARIABLE ff_status_output
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET)
    if(ff_status_result EQUAL 0)
      if(ff_status_output STREQUAL "")
        set(ff_tree_state "clean")
      else()
        set(ff_tree_state "dirty")
      endif()
    else()
      set(ff_tree_state "unbound")
    endif()

    execute_process(
      COMMAND git -C "${SOURCE_DIR}" show -s --format=%cI HEAD
      RESULT_VARIABLE ff_time_result
      OUTPUT_VARIABLE ff_commit_time
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET)
    if(NOT ff_time_result EQUAL 0)
      set(ff_commit_time "unbound")
    endif()
  endif()
endif()

# Optional early warning when an explicit override disagrees with the checkout.
if((DEFINED EXPECTED_BUILD_ID AND NOT EXPECTED_BUILD_ID STREQUAL "") OR
   (DEFINED ENV{FASTFILES_BUILD_ID} AND NOT "$ENV{FASTFILES_BUILD_ID}" STREQUAL ""))
  execute_process(
    COMMAND git -C "${SOURCE_DIR}" rev-parse HEAD
    RESULT_VARIABLE ff_verify_result
    OUTPUT_VARIABLE ff_git_head
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  if(ff_verify_result EQUAL 0)
    string(TOLOWER "${ff_git_head}" ff_git_head)
    if(NOT ff_git_head STREQUAL ff_build_id)
      message(WARNING
        "FASTFILES_BUILD_ID override (${ff_build_id}) differs from git HEAD (${ff_git_head}).")
    endif()
  endif()
endif()

set(ff_special_build "tree=${ff_tree_state};config=${CONFIGURATION};arch=${ARCHITECTURE};committed=${ff_commit_time}")

# Byte-stable LF rendering: write-if-changed only preserves mtimes when the
# rendered text is deterministic. Never emit wall-clock timestamps here.
set(ff_header_content
"")
string(APPEND ff_header_content
"#pragma once\n"
"\n"
"// Generated at build time by cmake/WriteBuildIdentity.cmake. Do not edit.\n"
"// Refreshed on every build so an incremental rebuild after a commit cannot\n"
"// silently keep a stale source identity.\n"
"namespace ffsetup {\n"
"\n"
"inline constexpr wchar_t kBuildIdentity[] = L\"${ff_build_id}\";\n"
"inline constexpr char kBuildIdentityAscii[] = \"${ff_build_id}\";\n"
"inline constexpr wchar_t kBuildWorktreeState[] = L\"${ff_tree_state}\";\n"
"inline constexpr char kBuildWorktreeStateAscii[] = \"${ff_tree_state}\";\n"
"inline constexpr wchar_t kBuildConfiguration[] = L\"${CONFIGURATION}\";\n"
"inline constexpr char kBuildConfigurationAscii[] = \"${CONFIGURATION}\";\n"
"inline constexpr wchar_t kBuildArchitecture[] = L\"${ARCHITECTURE}\";\n"
"inline constexpr char kBuildArchitectureAscii[] = \"${ARCHITECTURE}\";\n"
"inline constexpr wchar_t kBuildCommitTimestampUtc[] = L\"${ff_commit_time}\";\n"
"inline constexpr char kBuildCommitTimestampUtcAscii[] = \"${ff_commit_time}\";\n"
"\n"
"} // namespace ffsetup\n")

set(ff_rc_content
"")
string(APPEND ff_rc_content
"// Generated at build time by cmake/WriteBuildIdentity.cmake. Do not edit.\n"
"#ifndef FASTFILES_BUILD_IDENTITY_RC_H\n"
"#define FASTFILES_BUILD_IDENTITY_RC_H\n"
"\n"
"#define FF_BUILD_IDENTITY \"${ff_build_id}\"\n"
"#define FF_BUILD_WORKTREE_STATE \"${ff_tree_state}\"\n"
"#define FF_BUILD_CONFIGURATION \"${CONFIGURATION}\"\n"
"#define FF_BUILD_ARCHITECTURE \"${ARCHITECTURE}\"\n"
"#define FF_BUILD_COMMIT_TIMESTAMP \"${ff_commit_time}\"\n"
"#define FF_BUILD_SPECIAL \"${ff_special_build}\"\n"
"\n"
"#endif\n")

function(ff_write_if_changed target_path content)
  get_filename_component(ff_target_dir "${target_path}" DIRECTORY)
  if(NOT EXISTS "${ff_target_dir}")
    file(MAKE_DIRECTORY "${ff_target_dir}")
  endif()
  set(ff_tmp_path "${target_path}.tmp")
  file(WRITE "${ff_tmp_path}" "${content}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${ff_tmp_path}" "${target_path}"
    RESULT_VARIABLE ff_copy_result)
  file(REMOVE "${ff_tmp_path}")
  if(NOT ff_copy_result EQUAL 0)
    message(FATAL_ERROR "Failed to refresh build identity output: ${target_path}")
  endif()
endfunction()

ff_write_if_changed("${OUTPUT_HEADER}" "${ff_header_content}")
ff_write_if_changed("${OUTPUT_RC_DEFINES}" "${ff_rc_content}")

message(STATUS
  "FastFiles build identity: ${ff_build_id} (tree=${ff_tree_state}, config=${CONFIGURATION}, arch=${ARCHITECTURE})")
