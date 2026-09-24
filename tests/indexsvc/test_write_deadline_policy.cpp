#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "../TestSupport.h"

namespace {

std::filesystem::path FindRepositoryRoot() {
    std::filesystem::path current = std::filesystem::current_path();
    for (int depth = 0; depth < 8; ++depth) {
        if (std::filesystem::exists(current / "src" / "indexsvc" / "src" / "ServiceConnection.cpp")) {
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

void TestServiceUsesBoundedSharedWrites() {
    const auto root = FindRepositoryRoot();
    fftest::Check(!root.empty(), "service write-deadline policy test resolves the repository root");
    if (root.empty()) return;

    const std::string deadline = ReadText(root / "src" / "indexsvc" / "src" / "WriteDeadline.h");
    fftest::Check(deadline.find("WriteFrameOverlapped") != std::string::npos,
                  "service write helper delegates to shared overlapped framing");
    fftest::Check(deadline.find("steady_clock::now() + kWriteDeadline") != std::string::npos,
                  "service write helper supplies an absolute per-frame deadline");
    for (const std::string forbidden : {"std::thread", "CancelSynchronousIo", "DuplicateHandle",
                                        "RunWithWriteDeadline", "WriteDeadlineState"}) {
        const std::string description =
            "service write helper has no synchronous helper-thread fallback: " + forbidden;
        fftest::Check(deadline.find(forbidden) == std::string::npos,
                      description.c_str());
    }

    for (const std::string filename : {"ServiceConnection.cpp", "VolumeScanner.cpp", "UsnJournalReader.cpp"}) {
        const std::string source = ReadText(root / "src" / "indexsvc" / "src" / filename);
        const std::string helperDescription = filename + " routes live writes through the bounded service helper";
        const std::string bypassDescription = filename + " does not bypass the bounded service write helper";
        const std::string joinDescription = filename + " does not wrap writes in an unbounded helper-thread join";
        fftest::Check(source.find("WriteFrameWithDeadline") != std::string::npos,
                      helperDescription.c_str());
        fftest::Check(source.find("ffipc::WriteFrameOverlapped") == std::string::npos,
                      bypassDescription.c_str());
        fftest::Check(source.find("RunWithWriteDeadline") == std::string::npos,
                      joinDescription.c_str());
    }

    const std::string listener = ReadText(root / "src" / "ipc" / "src" / "PipeListener.cpp");
    fftest::Check(listener.find("FILE_FLAG_OVERLAPPED") != std::string::npos,
                  "shared pipe listener always creates overlapped instances");
    fftest::Check(listener.find("CancelSynchronousIo") == std::string::npos,
                  "shared pipe listener has no synchronous cancellation fallback");

    // residual-defect-closure-and-certification-completion 1.1: a failed
    // frame write is connection-fatal. It must never append another frame to
    // a possibly desynchronized byte stream.
    const std::string scanner = ReadText(root / "src" / "indexsvc" / "src" / "VolumeScanner.cpp");
    fftest::Check(scanner.find("ScanRunResult RunVolumeScan(") != std::string::npos,
                  "scanner exposes an explicit run outcome");
    fftest::Check(scanner.find("return ScanRunResult::TransportFailed") != std::string::npos,
                  "a failed scan write returns a transport-fatal outcome");
    const std::size_t failureGuard = scanner.find("if (writeFailed)");
    fftest::Check(failureGuard != std::string::npos,
                  "mid-scan write failure has a dedicated outcome branch");
    if (failureGuard != std::string::npos) {
        const std::size_t failureEnd = scanner.find("return ScanRunResult::TransportFailed;", failureGuard);
        fftest::Check(failureEnd != std::string::npos,
                      "transport-fatal branch has an explicit return");
        if (failureEnd != std::string::npos) {
            const std::string failureBlock = scanner.substr(failureGuard, failureEnd - failureGuard);
            fftest::Check(failureBlock.find("recordFailureDiagnostic") != std::string::npos,
                          "transport-fatal write failure records a metadata diagnostic");
            fftest::Check(failureBlock.find("SendScanTerminal") == std::string::npos,
                          "transport-fatal write failure never appends a terminal frame");
        }
    }
    fftest::Check(scanner.find("return ScanRunResult::Cancelled;") != std::string::npos,
                  "cancellation remains a silent outcome");
    fftest::Check(scanner.find("recordFailureDiagnostic(L\"scan-terminal-write-failed\"") != std::string::npos,
                  "a failed terminal write is diagnosed as transport-fatal");
}

} // namespace

int main() {
    TestServiceUsesBoundedSharedWrites();
    if (fftest::FailureCount() > 0) {
        std::fprintf(stderr, "%d test(s) failed\n", fftest::FailureCount());
        return 1;
    }
    std::printf("All service write-deadline policy tests passed.\n");
    return 0;
}
