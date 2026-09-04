# ntfs-observe certification probe (FC-003/FC-004): bounded read-only
# MFT/USN observer reusing the production VolumeScanner/UsnJournalReader
# ioctl sequences plus the production ffmftparser/ffusnparser parsers.
# Static so the contract unit tests link the same code fftest ships.
add_library(ffntfsobserve STATIC
  src/NtfsObserve.cpp
)
target_include_directories(ffntfsobserve PUBLIC src)
target_compile_features(ffntfsobserve PUBLIC cxx_std_20)
target_link_libraries(ffntfsobserve PUBLIC ffindexsvcprobes ffmftparser ffusnparser)

add_executable(fftest
  src/Main.cpp
)

fastfiles_add_version_resource(fftest "fftest.exe" "FastFiles diagnostic validation probe")

target_link_libraries(fftest PRIVATE ffprotocol ffipc ffsetup ffindexsvcprobes ffindexstore ffntfsobserve)
