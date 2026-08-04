#include <cstdio>
#include <cstdlib>

#include "ConnectionRegistry.h"
#include "ffprotocol/Commands.h"

namespace {
int failures = 0;

void Check(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    } else {
        std::printf("ok: %s\n", message);
    }
}
}  // namespace

int main() {
    using ffindexsvc::ConnectionRegistry;
    using ffprotocol::VolumeId;

    constexpr ConnectionRegistry::ConnectionId connA = 1;
    constexpr ConnectionRegistry::ConnectionId connB = 2;
    const VolumeId volume1{1};
    const VolumeId volume2{2};

    // --- Volume scan ownership ---
    {
        ConnectionRegistry registry;

        // First connection may start a scan on an unowned volume.
        Check(registry.TryMarkVolumeScanStarted(connA, volume1),
              "scan: first connection may start a scan on an unowned volume");

        // A different live connection must NOT supersede connA's scan.
        Check(!registry.TryMarkVolumeScanStarted(connB, volume1),
              "scan: a different connection is rejected from superseding another's scan");

        // The owning connection may re-start / supersede its own scan.
        Check(registry.TryMarkVolumeScanStarted(connA, volume1),
              "scan: owner may re-start / supersede its own scan");

        // Stop must be authorized by the owner.
        Check(!registry.TryStopVolumeScan(connB, volume1),
              "scan: non-owner Stop is rejected");
        Check(registry.TryStopVolumeScan(connA, volume1),
              "scan: owner Stop is accepted");

        // After the owner stops, another connection may take over.
        Check(registry.TryMarkVolumeScanStarted(connB, volume1),
              "scan: after owner stops, another connection may start a scan");
    }

    // --- USN journal ownership ---
    {
        ConnectionRegistry registry;

        Check(registry.TryMarkUsnJournalOpened(connA, volume1),
              "journal: first connection may open a journal on an unowned volume");
        Check(!registry.TryMarkUsnJournalOpened(connB, volume1),
              "journal: a different connection is rejected from superseding another's journal");
        Check(registry.TryMarkUsnJournalOpened(connA, volume1),
              "journal: owner may re-open / supersede its own journal");
        Check(!registry.TryCloseUsnJournal(connB, volume1),
              "journal: non-owner Close is rejected");
        Check(registry.TryCloseUsnJournal(connA, volume1),
              "journal: owner Close is accepted");
        Check(registry.TryMarkUsnJournalOpened(connB, volume1),
              "journal: after owner closes, another connection may open a journal");
    }

    // --- Teardown clears the owner's entries, independent of volume ---
    {
        ConnectionRegistry registry;
        Check(registry.TryMarkVolumeScanStarted(connA, volume1),
              "teardown: connA starts a scan on volume1");
        Check(registry.TryMarkVolumeScanStarted(connA, volume2),
              "teardown: connA starts a scan on volume2");
        Check(registry.TryMarkUsnJournalOpened(connA, volume1),
              "teardown: connA opens a journal on volume1");

        // Disconnect tears down all of connA's entries.
        registry.TeardownConnection(connA);

        Check(registry.TryMarkVolumeScanStarted(connB, volume1),
              "teardown: after connA disconnects, connB may start a scan on volume1");
        Check(registry.TryMarkVolumeScanStarted(connB, volume2),
              "teardown: after connA disconnects, connB may start a scan on volume2");
        Check(registry.TryMarkUsnJournalOpened(connB, volume1),
              "teardown: after connA disconnects, connB may open a journal on volume1");
    }

    // --- Ownership is per-volume: a different connection may use a different volume ---
    {
        ConnectionRegistry registry;
        Check(registry.TryMarkVolumeScanStarted(connA, volume1),
              "per-volume: connA owns volume1 scan");
        Check(registry.TryMarkVolumeScanStarted(connB, volume2),
              "per-volume: connB may still scan a different volume (volume2)");
        Check(!registry.TryMarkVolumeScanStarted(connB, volume1),
              "per-volume: connB is still rejected on volume1");
    }

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}