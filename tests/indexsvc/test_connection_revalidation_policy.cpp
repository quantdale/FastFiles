#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "../TestSupport.h"
#include "CommandSurface.h"

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

// hardening-campaign Phase 7 H1: red-first policy reproduction for the
// wall-clock revalidation cadence. These assertions FAIL against the
// heartbeat-only implementation (client-owned cadence) and pass once the
// dispatch loop proves authorization validity from the service's own clock.
// The ShouldRevalidate predicate transition matrix is asserted by the same
// suite once the predicate lands with the implementation changeset.
void TestRevalidationCadencePolicy() {
    const auto root = FindRepositoryRoot();
    fftest::Check(!root.empty(), "revalidation policy test resolves the repository root");
    if (root.empty()) return;

    const std::string source = ReadText(root / "src" / "indexsvc" / "src" / "ServiceConnection.cpp");
    const std::string surface = ReadText(root / "src" / "indexsvc" / "src" / "CommandSurface.h");

    // Structural presence: the cadence state exists at all.
    fftest::Check(source.find("kRevalidationInterval") != std::string::npos,
                  "dispatch loop declares a wall-clock revalidation interval");
    fftest::Check(source.find("lastRevalidated") != std::string::npos,
                  "connection tracks its last successful revalidation time");

    // The client must no longer own the cadence: RevalidateClient must be
    // invoked from the dispatch flow beyond the single Heartbeat case.
    std::size_t first = source.find("RevalidateClient(");
    std::size_t second = (first == std::string::npos) ? std::string::npos
                                                      : source.find("RevalidateClient(", first + 1);
    fftest::Check(second != std::string::npos,
                  "identity revalidation runs from the dispatch cadence, not only the Heartbeat case");

    // The pure predicate seam lives header-inline beside the allowlist gate.
    fftest::Check(surface.find("ShouldRevalidate") != std::string::npos,
                  "header-inline ShouldRevalidate cadence predicate exists");

    // Behavioral transition matrix for the pure cadence predicate: fresh ->
    // no check; before-boundary -> no check; exact boundary -> check (closed
    // interval, staleness starts AT the boundary); long-idle -> check (the
    // client-owned-cadence bypass is closed); just-refreshed -> restart span.
    const std::chrono::steady_clock::time_point t0 =
        std::chrono::steady_clock::time_point{} + std::chrono::seconds(100);
    const std::chrono::milliseconds interval{20000};
    fftest::Check(!ffindexsvc::ShouldRevalidate(t0, t0, interval),
                  "fresh connection does not revalidate before the interval elapses");
    fftest::Check(!ffindexsvc::ShouldRevalidate(t0 + interval - std::chrono::milliseconds(1), t0, interval),
                  "one millisecond before the boundary stays within the attested span");
    fftest::Check(ffindexsvc::ShouldRevalidate(t0 + interval, t0, interval),
                  "the exact interval boundary demands revalidation");
    fftest::Check(ffindexsvc::ShouldRevalidate(t0 + interval * 3, t0, interval),
                  "long-idle traffic is revalidated, closing the client-owned cadence bypass");
    fftest::Check(!ffindexsvc::ShouldRevalidate(t0, t0 + interval, interval),
                  "a just-refreshed timestamp restarts the attested span");
    fftest::Check(ffindexsvc::ShouldRevalidate(t0 + std::chrono::milliseconds(100), t0,
                                               std::chrono::milliseconds(100)),
                  "the heartbeat freshness epsilon also uses the closed interval");

    // Existing behavior that must not regress: Heartbeat still revalidates
    // and the allowlist gate still precedes dispatch.
    fftest::Check(source.find("case MessageType::Heartbeat") != std::string::npos,
                  "Heartbeat case remains the explicit revalidation event");
    fftest::Check(source.find("IsAllowedClientRequest") != std::string::npos,
                  "closed-command allowlist gate remains in the dispatch loop");
}

} // namespace

int main() {
    TestRevalidationCadencePolicy();
    if (fftest::FailureCount() > 0) {
        std::fprintf(stderr, "%d test(s) failed\n", fftest::FailureCount());
        return 1;
    }
    std::printf("All connection revalidation cadence policy tests passed.\n");
    return 0;
}
