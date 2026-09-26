#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "../TestSupport.h"

namespace {

std::filesystem::path FindRepositoryRoot() {
    std::filesystem::path current = std::filesystem::current_path();
    for (int depth = 0; depth < 8; ++depth) {
        if (std::filesystem::exists(current / "src" / "engine" / "src" / "PrivilegedConnection.cpp")) {
            return current;
        }
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

void CollectCMakeFiles(const std::filesystem::path& path, std::vector<std::filesystem::path>& files) {
    if (!std::filesystem::exists(path)) return;
    if (std::filesystem::is_regular_file(path)) {
        if (path.filename() == "CMakeLists.txt" || path.extension() == ".cmake") files.push_back(path);
        return;
    }

    const std::filesystem::recursive_directory_iterator end;
    for (std::filesystem::recursive_directory_iterator iterator(path); iterator != end; ++iterator) {
        if (iterator->is_directory() &&
            (iterator->path().filename() == "build" || iterator->path().filename() == ".git")) {
            iterator.disable_recursion_pending();
            continue;
        }
        if (iterator->is_regular_file() &&
            (iterator->path().filename() == "CMakeLists.txt" || iterator->path().extension() == ".cmake")) {
            files.push_back(iterator->path());
        }
    }
}

void TestPrivilegedConnectionTestSeamScopeAndProductionFallback() {
    const auto root = FindRepositoryRoot();
    fftest::Check(!root.empty(), "test-seam policy test resolves the repository root");
    if (root.empty()) return;

    const std::string productionCmake = ReadText(root / "src" / "engine" / "CMakeLists.txt");
    const std::string focusedTestCmake = ReadText(root / "tests" / "engine" / "CMakeLists.txt");
    const std::string implementation = ReadText(root / "src" / "engine" / "src" / "PrivilegedConnection.cpp");
    const std::string header = ReadText(root / "src" / "engine" / "src" / "PrivilegedConnection.h");

    std::vector<std::filesystem::path> cmakeFiles;
    CollectCMakeFiles(root / "CMakeLists.txt", cmakeFiles);
    CollectCMakeFiles(root / "cmake", cmakeFiles);
    CollectCMakeFiles(root / "src", cmakeFiles);
    CollectCMakeFiles(root / "tests", cmakeFiles);
    constexpr char kTestMacro[] = "FF_PRIVILEGED_CONNECTION_TESTING";
    size_t macroFileCount = 0;
    std::string macroDefinitionFile;
    for (const auto& path : cmakeFiles) {
        const std::string content = ReadText(path);
        if (content.find(kTestMacro) == std::string::npos) continue;
        ++macroFileCount;
        macroDefinitionFile = path.lexically_relative(root).generic_string();
    }
    fftest::Check(macroFileCount == 1 && macroDefinitionFile == "tests/engine/CMakeLists.txt",
                  "the test macro appears in exactly one CMake file, the focused engine test file");

    const std::regex focusedTargetDefinition(
        R"(target_compile_definitions\s*\(\s*ffengine_volume_session_manager_tests\s+PRIVATE\s+FF_PRIVILEGED_CONNECTION_TESTING\s*=\s*1\s*\))");
    fftest::Check(std::regex_search(focusedTestCmake, focusedTargetDefinition),
                  "only ffengine_volume_session_manager_tests receives the test-only definition");
    fftest::Check(productionCmake.find("FastFilesEngine") != std::string::npos &&
                      productionCmake.find(kTestMacro) == std::string::npos,
                  "the production FastFilesEngine target does not receive the test-only definition");

    const std::size_t testGate = implementation.find("#ifdef FF_PRIVILEGED_CONNECTION_TESTING");
    const std::size_t testVerifier = implementation.find("if (serviceIdentityVerifier_)", testGate);
    const std::size_t testGateEnd = implementation.find("#endif", testVerifier);
    const std::size_t productionFallback = implementation.find("ULONG serverPid", testGateEnd);
    const std::size_t imagePath = implementation.find("GetProcessImagePath(process)", productionFallback);
    const std::size_t installedImageCheck = implementation.find(
        "IsExpectedInstalledBinary(*imagePath, installDir_, ffsetup::kIndexSvcExeName)", imagePath);
    const std::size_t pinnedSignatureCheck = implementation.find(
        "VerifyPinnedSignature(*imagePath, ffsetup::kExpectedIndexSvcSignatureThumbprint)", installedImageCheck);
    fftest::Check(testGate < testVerifier && testVerifier < testGateEnd && testGateEnd < productionFallback &&
                      productionFallback < imagePath && imagePath < installedImageCheck &&
                      installedImageCheck < pinnedSignatureCheck,
                  "production fallback still verifies the service image path and pinned signature after the test gate");

    const std::size_t headerTestGate = header.find("#ifdef FF_PRIVILEGED_CONNECTION_TESTING");
    const std::size_t verifierSetter = header.find("SetServiceIdentityVerifierForTesting", headerTestGate);
    const std::size_t pipeSetter = header.find("SetControlPipeNameForTesting", verifierSetter);
    const std::size_t headerTestGateEnd = header.find("#endif", pipeSetter);
    fftest::Check(headerTestGate < verifierSetter && verifierSetter < pipeSetter && pipeSetter < headerTestGateEnd,
                  "the verifier and pipe-name seams are both excluded unless the test macro is defined");
}

} // namespace

int main() {
    TestPrivilegedConnectionTestSeamScopeAndProductionFallback();
    if (fftest::FailureCount() > 0) {
        std::fprintf(stderr, "%d test(s) failed\n", fftest::FailureCount());
        return 1;
    }
    std::printf("All privileged connection test-seam policy checks passed.\n");
    return 0;
}
