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
if(NOT FASTFILES_PRODUCT_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
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

if(NOT DEFINED FASTFILES_GENERATED_INCLUDE_DIR)
  message(FATAL_ERROR "FASTFILES_GENERATED_INCLUDE_DIR must be set before FastFilesVersion.cmake")
endif()
configure_file(
  "${CMAKE_SOURCE_DIR}/cmake/FastFilesVersion.h.in"
  "${FASTFILES_GENERATED_INCLUDE_DIR}/ffsetup/FastFilesVersion.h"
  @ONLY)

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
  target_sources("${target}" PRIVATE "${resource_path}")
endfunction()
