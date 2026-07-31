cmake_minimum_required(VERSION 3.24)
project(FastFiles LANGUAGES CXX)

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

if(MSVC)
  add_compile_options(/W4 /WX /permissive- /sdl /guard:cf /Zc:__cplusplus)
  add_link_options(/DYNAMICBASE /NXCOMPAT /guard:cf)
endif()

option(FASTFILES_BUILD_TESTS "Build FastFiles unit tests" ON)

add_subdirectory(src/protocol)
add_subdirectory(src/ipc)
add_subdirectory(src/setup)
add_subdirectory(src/indexsvc)
add_subdirectory(src/engine)
add_subdirectory(src/ui)
add_subdirectory(src/installer)

if(FASTFILES_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
