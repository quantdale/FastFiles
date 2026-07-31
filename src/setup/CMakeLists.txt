add_library(ffsetup STATIC
  src/SecurityDescriptors.cpp
  src/ServiceRegistration.cpp
  src/GroupSetup.cpp
  src/InstallDirAcl.cpp
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
