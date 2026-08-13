#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "../TestSupport.h"

namespace {

std::filesystem::path FindRepositoryRoot() {
    std::filesystem::path current = std::filesystem::current_path();
    for (int depth = 0; depth < 8; ++depth) {
        if (std::filesystem::exists(current / "src" / "installer" / "src" / "Main.cpp")) return current;
        if (current == current.root_path()) break;
        current = current.parent_path();
    }
    return {};
}

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

void TestNoUserWritableUninstallRelay() {
    const auto root = FindRepositoryRoot();
    fftest::Check(!root.empty(), "installer source-policy test resolves the repository root");
    if (root.empty()) return;
    const std::string mainSource = ReadText(root / "src" / "installer" / "src" / "Main.cpp");
    fftest::Check(mainSource.find("GetTempPathW") == std::string::npos,
                  "uninstall does not derive an elevated relay path from %TEMP%");
    fftest::Check(mainSource.find("FastFilesSetup-Uninstall-") == std::string::npos,
                  "uninstall has no predictable temporary relay filename");
    fftest::Check(mainSource.find("RelayInstalledUninstall") == std::string::npos &&
                      mainSource.find("CreateProcessW") == std::string::npos,
                  "uninstall does not spawn a copied helper executable");
    fftest::Check(mainSource.find("ScheduleSelfDeletion") != std::string::npos &&
                      mainSource.find("MOVEFILE_DELAY_UNTIL_REBOOT") != std::string::npos,
                  "self-removal uses a trusted installed image and system-managed deferred deletion");
}

} // namespace

int wmain() {
    TestNoUserWritableUninstallRelay();
    if (fftest::FailureCount() > 0) {
        std::fprintf(stderr, "%d test(s) failed\n", fftest::FailureCount());
        return 1;
    }
    std::printf("All installer source-policy tests passed.\n");
    return 0;
}
