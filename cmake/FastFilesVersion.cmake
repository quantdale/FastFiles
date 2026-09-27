# The repository VERSION file is the single source for the product version.
# CMake validates all four Windows VERSIONINFO components before generating
# resources and the installer-facing version header.
set(FASTFILES_PRODUCT_NAME "FastFiles")
set(FASTFILES_COMPANY_NAME "FastFiles")
set(FASTFILES_PUBLISHER_NAME "${FASTFILES_COMPANY_NAME}")
set(FASTFILES_VERSION_FILE "${CMAKE_SOURCE_DIR}/VERSION")
if(NOT EXISTS "${FASTFILES_VERSION_FILE}")
  message(FATAL_ERROR "FastFiles VERSION file is missing: ${FASTFILES_VERSION_FILE}")
endif()

file(STRINGS "${FASTFILES_VERSION_FILE}" FASTFILES_PRODUCT_VERSION LIMIT_COUNT 1)
string(STRIP "${FASTFILES_PRODUCT_VERSION}" FASTFILES_PRODUCT_VERSION)
if(NOT FASTFILES_PRODUCT_VERSION MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$")
  message(FATAL_ERROR
    "FastFiles VERSION must contain exactly MAJOR.MINOR.PATCH numeric components.")
endif()

string(REPLACE "." ";" FASTFILES_VERSION_COMPONENTS "${FASTFILES_PRODUCT_VERSION}")
list(GET FASTFILES_VERSION_COMPONENTS 0 FASTFILES_VERSION_MAJOR)
list(GET FASTFILES_VERSION_COMPONENTS 1 FASTFILES_VERSION_MINOR)
list(GET FASTFILES_VERSION_COMPONENTS 2 FASTFILES_VERSION_PATCH)
foreach(component_name IN ITEMS MAJOR MINOR PATCH)
  if(FASTFILES_VERSION_${component_name} GREATER 65535)
    message(FATAL_ERROR
      "FastFiles ${component_name} version component exceeds the Windows VERSIONINFO limit.")
  endif()
endforeach()

set(FASTFILES_FILE_VERSION "${FASTFILES_PRODUCT_VERSION}.0")
set(FASTFILES_VERSION_COMMA
  "${FASTFILES_VERSION_MAJOR},${FASTFILES_VERSION_MINOR},${FASTFILES_VERSION_PATCH},0")

# Source/build identity is refreshed on every build by
# cmake/WriteBuildIdentity.cmake (always-run custom target below). It is NOT
# derived at configure time, so a normal incremental rebuild after a commit can
# never keep a stale PrivateBuild / kBuildIdentity. A caller may still pin an
# explicit CI identity through the FASTFILES_BUILD_ID environment variable; the
# build-time script validates it fail-closed.
#
# At configure time we only fail early when an obviously malformed override is
# already visible, so misconfiguration surfaces before the first compile.
if(DEFINED ENV{FASTFILES_BUILD_ID})
  set(ff_configured_build_id "$ENV{FASTFILES_BUILD_ID}")
  string(STRIP "${ff_configured_build_id}" ff_configured_build_id)
  string(LENGTH "${ff_configured_build_id}" ff_configured_build_id_length)
  if(NOT ff_configured_build_id_length EQUAL 40 OR NOT ff_configured_build_id MATCHES "^[0-9a-fA-F]+$")
    message(FATAL_ERROR
      "FASTFILES_BUILD_ID must be a non-empty 40-character Git identity when set.")
  endif()
endif()

if(NOT DEFINED FASTFILES_GENERATED_INCLUDE_DIR)
  message(FATAL_ERROR "FASTFILES_GENERATED_INCLUDE_DIR must be set before FastFilesVersion.cmake")
endif()

set(FASTFILES_BUILD_IDENTITY_HEADER
  "${FASTFILES_GENERATED_INCLUDE_DIR}/ffsetup/BuildIdentity.h")
set(FASTFILES_BUILD_IDENTITY_RC_DEFINES
  "${FASTFILES_GENERATED_INCLUDE_DIR}/ffsetup/BuildIdentity.rc.h")

# Static product constants stay configure-time (VERSION-derived). Identity is
# deliberately excluded from this header; FastFilesVersion.h includes the
# build-time generated BuildIdentity.h instead.
configure_file(
  "${CMAKE_SOURCE_DIR}/cmake/FastFilesVersion.h.in"
  "${FASTFILES_GENERATED_INCLUDE_DIR}/ffsetup/FastFilesVersion.h"
  @ONLY)

# Best-effort architecture label for identity metadata (informational only).
set(ff_identity_architecture "unknown")
if(CMAKE_GENERATOR_PLATFORM)
  string(TOLOWER "${CMAKE_GENERATOR_PLATFORM}" ff_identity_architecture)
elseif(DEFINED CMAKE_CXX_COMPILER_ARCHITECTURE_ID AND NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "")
  string(TOLOWER "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}" ff_identity_architecture)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(ff_identity_architecture "x64")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
  set(ff_identity_architecture "x86")
endif()

# Always-run identity refresh. Outputs are rewritten only when bytes change,
# so unchanged identity preserves mtimes and forces zero recompiles; a new
# commit moves the header/rc-define mtimes and bounds the rebuild to the
# identity consumers (version resources + identity-including TUs).
if(NOT TARGET fastfiles_build_identity)
  set(ff_identity_configuration "unknown")
  if(NOT "${CMAKE_BUILD_TYPE}" STREQUAL "")
    set(ff_identity_configuration "${CMAKE_BUILD_TYPE}")
  endif()
  add_custom_target(fastfiles_build_identity ALL
    COMMAND "${CMAKE_COMMAND}"
      "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}"
      "-DOUTPUT_HEADER=${FASTFILES_BUILD_IDENTITY_HEADER}"
      "-DOUTPUT_RC_DEFINES=${FASTFILES_BUILD_IDENTITY_RC_DEFINES}"
      "-DCONFIGURATION=${ff_identity_configuration}"
      "-DARCHITECTURE=${ff_identity_architecture}"
      -P "${CMAKE_SOURCE_DIR}/cmake/WriteBuildIdentity.cmake"
    BYPRODUCTS
      "${FASTFILES_BUILD_IDENTITY_HEADER}"
      "${FASTFILES_BUILD_IDENTITY_RC_DEFINES}"
    COMMENT "Refreshing FastFiles build identity"
    VERBATIM)
endif()

function(fastfiles_add_version_resource target original_filename file_description)
  if(NOT TARGET "${target}")
    message(FATAL_ERROR "Cannot add version metadata to unknown target: ${target}")
  endif()
  set(FASTFILES_VERSION_ORIGINAL_FILENAME "${original_filename}")
  set(FASTFILES_VERSION_FILE_DESCRIPTION "${file_description}")
  set(resource_path "${CMAKE_CURRENT_BINARY_DIR}/${target}-version.rc")
  configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/FastFilesVersion.rc.in"
    "${resource_path}"
    @ONLY)
  set_source_files_properties("${resource_path}" PROPERTIES GENERATED TRUE)
  # The .rc includes the build-time identity defines; OBJECT_DEPENDS forces the
  # resource to recompile exactly when the identity bytes change.
  set_source_files_properties("${resource_path}" PROPERTIES
    OBJECT_DEPENDS "${FASTFILES_BUILD_IDENTITY_RC_DEFINES}")
  target_sources("${target}" PRIVATE "${resource_path}")
  # The generated identity header/rc-defines must exist before first compile.
  add_dependencies("${target}" fastfiles_build_identity)
endfunction()
