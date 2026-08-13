cmake_minimum_required(VERSION 3.25)
# C is enabled solely to compile the vendored SQLite amalgamation
# (third_party/sqlite); all first-party FastFiles code is C++20.
project(FastFiles LANGUAGES CXX C)

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
