# Diagnostic-only escape hatch for the pinned Authenticode checks
# (AuthenticodeVerification.cpp). Never enabled in a shipped configuration:
# Release and any distribution candidate must keep the fail-closed path
# compiled in, so the option is rejected outside Debug/analyze builds.
option(FASTFILES_DIAGNOSTIC_ALLOW_UNSIGNED
  "Allow unsigned binaries in diagnostic-only builds (mutual authentication checks pass unconditionally)" OFF)

if(FASTFILES_DIAGNOSTIC_ALLOW_UNSIGNED)
  if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(FATAL_ERROR
      "FASTFILES_DIAGNOSTIC_ALLOW_UNSIGNED may only be enabled in Debug builds; "
      "a ${CMAKE_BUILD_TYPE} build cannot be a diagnostic build.")
  endif()
  target_compile_definitions(ffsetup PRIVATE FASTFILES_DIAGNOSTIC_ALLOW_UNSIGNED)
endif()

add_library(ffsetup STATIC
  src/SecurityDescriptors.cpp
  src/ServiceRegistration.cpp
  src/GroupSetup.cpp
  src/InstallDirAcl.cpp
  src/PeerImageMatch.cpp
  src/ScheduledTaskRegistration.cpp
  src/AuthenticodeVerification.cpp
)

target_include_directories(ffsetup PUBLIC include)
target_compile_features(ffsetup PUBLIC cxx_std_20)

target_link_libraries(ffsetup PUBLIC
  advapi32
  netapi32
  ole32
  oleaut32
  wintrust
  crypt32
  taskschd
  comsuppw
)
