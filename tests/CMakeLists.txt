add_subdirectory(protocol)
add_subdirectory(ipc)
add_subdirectory(indexstore)
add_subdirectory(search)
add_subdirectory(indexsvc)
add_subdirectory(engine)
add_subdirectory(navigation)
add_subdirectory(commands)
add_subdirectory(fileoperations)
add_subdirectory(preview)
add_subdirectory(ui)
add_subdirectory(uia-driver)
add_subdirectory(setup)
add_subdirectory(drift)

# CTest test properties are directory-scoped since CMake 3.29: a
# set_tests_properties() call here cannot see tests registered by the
# add_subdirectory() children below. Every component therefore sets
# TIMEOUT 120 on its own tests in its tests/<component>/CMakeLists.txt (the
# uia-driver/drift PowerShell tests set their own timeouts where they are
# registered).
