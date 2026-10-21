#pragma once

// tests/ui/ClipboardRoundTripClassifier.h
// large-index-lifecycle-and-search-scalability review-remediation (P2-B):
// deterministic classification of real-clipboard round-trip observations.
// Pure data-in / verdict-out so every failure mode is unit-testable without
// touching the host clipboard (the live round-trip block collects raw Win32
// observations and classifies through this helper). The superseded inline
// discriminator treated "CF_HDROP is still advertised" as proof of
// environment read interception and skipped BOTH assertions, so genuine
// regressions (wrong paths, malformed HDROP payloads, missing or wrong
// Preferred DropEffect, cut/copy flips) passed silently. Verdict rules:
//
//   VALID ROUND TRIP                    -> Pass
//   READABLE BUT WRONG/INCOMPLETE DATA  -> DataWrongOrIncomplete (FAIL)
//   CONFIRMED ENVIRONMENT INTERCEPTION  -> EnvironmentInterception (SKIP)
//   WRITE/OPEN REFUSED BY THE SESSION   -> PublicationUnavailable (SKIP,
//                                          the pre-existing locked-down
//                                          session accommodation)
//   ANY OTHER FAILURE                   -> Undetermined (FAIL)
//
// A verdict of Skip is only reachable from CONFIRMED read interception (every
// advertised format -- including an independent format such as
// CF_UNICODETEXT -- delivered a null handle while the write stuck) or from a
// refused write/open. Format availability alone never proves a round trip and
// never converts a failure into a skip.

#include <cstdint>

namespace fftest {

enum class ClipboardRoundTripVerdict {
    Pass,
    DataWrongOrIncomplete,
    EnvironmentInterception,
    PublicationUnavailable,
    Undetermined,
};

inline const char* ClipboardRoundTripVerdictName(const ClipboardRoundTripVerdict verdict) {
    if (verdict == ClipboardRoundTripVerdict::Pass) return "PASS";
    if (verdict == ClipboardRoundTripVerdict::DataWrongOrIncomplete) return "FAIL(data wrong or incomplete)";
    if (verdict == ClipboardRoundTripVerdict::EnvironmentInterception) {
        return "SKIP(environment read interception)";
    }
    if (verdict == ClipboardRoundTripVerdict::PublicationUnavailable) return "SKIP(clipboard unavailable)";
    if (verdict == ClipboardRoundTripVerdict::Undetermined) return "FAIL(undetermined)";
    return "FAIL(unknown)";
}

// Raw observations gathered with Win32 calls by the caller. The mismatch-path
// probes are only collected when the round trip did not return the exact
// published data; the happy path never needs them.
struct ClipboardRoundTripObservations {
    // Round-trip outcome (from the product read path).
    bool writePublished = false;            // CopyFilesToClipboard returned true (SetClipboardData accepted)
    bool pathsMatchExpected = false;        // parsed CF_HDROP paths == published paths (exact)
    bool cutIntentMatchesExpected = false;  // cut flag == published cut intent (both directions matter)
    // Raw probes (mismatch path only).
    bool hdropAdvertised = false;           // IsClipboardFormatAvailable(CF_HDROP)
    bool hdropReadable = false;             // GetClipboardData(CF_HDROP) non-null and GlobalLock succeeded
    bool hdropPayloadValid = false;         // the locked payload parsed as a well-formed path list
    uint32_t advertisedFormatCount = 0;     // probe formats enumerated as available
    uint32_t nullHandleFormatCount = 0;     // advertised formats whose GetClipboardData returned null (or unparseable)
    uint32_t readableFormatCount = 0;       // advertised formats that returned a readable handle
    bool independentFormatAdvertised = false; // an independent format (CF_UNICODETEXT) is advertised
    bool independentFormatReadable = false;   // and delivered a readable handle
    bool sequenceChangedSincePublication = false; // GetClipboardSequenceNumber moved between write and read
};

inline ClipboardRoundTripVerdict ClassifyClipboardRoundTrip(
    const ClipboardRoundTripObservations& observations) {
    if (!observations.writePublished) {
        return ClipboardRoundTripVerdict::PublicationUnavailable;
    }
    if (observations.pathsMatchExpected && observations.cutIntentMatchesExpected) {
        return ClipboardRoundTripVerdict::Pass;
    }
    // Mismatch: only CONFIRMED session-level read interception may skip. Its
    // measured signature (raw Win32 probe recorded in the change evidence):
    // the write sticks and formats stay advertised, but GetClipboardData
    // delivers a null handle for EVERY advertised format -- including
    // independent formats such as raw CF_UNICODETEXT -- so nothing at all
    // could be read back. An advertised-but-unreadable CF_HDROP alongside a
    // readable independent format is NOT this signature: that is an
    // undetermined failure and must fail.
    const bool everyAdvertisedReadNull = observations.advertisedFormatCount > 0 &&
        observations.nullHandleFormatCount == observations.advertisedFormatCount &&
        observations.readableFormatCount == 0;
    const bool interceptionConfirmed = observations.hdropAdvertised &&
        observations.independentFormatAdvertised && everyAdvertisedReadNull &&
        !observations.hdropReadable;
    if (interceptionConfirmed) {
        return ClipboardRoundTripVerdict::EnvironmentInterception;
    }
    if (observations.sequenceChangedSincePublication) {
        // Another writer touched the clipboard between publication and read.
        // That cannot be told apart from a product regression from here, so it
        // may never silently pass or take the interception skip branch.
        return ClipboardRoundTripVerdict::Undetermined;
    }
    if (observations.hdropReadable) {
        // The payload was delivered: wrong paths, a malformed list, or a
        // missing/wrong Preferred DropEffect are product-visible data defects.
        return ClipboardRoundTripVerdict::DataWrongOrIncomplete;
    }
    return ClipboardRoundTripVerdict::Undetermined;
}

// The superseded discriminator this change replaces: any failed round trip
// "passed" (skipped both assertions) whenever CF_HDROP was merely advertised.
// Retained only so the negative controls can PROVE the old rule
// misclassifies genuine data failures as skips (non-vacuity). Never used by
// the real-clipboard block.
inline bool LegacyFormatAvailableWouldSkip(const bool readSucceeded, const bool hdropAdvertised) {
    return !readSucceeded && hdropAdvertised;
}

} // namespace fftest
