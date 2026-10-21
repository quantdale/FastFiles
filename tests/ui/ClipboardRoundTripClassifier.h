#pragma once

// tests/ui/ClipboardRoundTripClassifier.h
// large-index-lifecycle-and-search-scalability review-remediation (P2-B),
// with adversarial-review lane-B remediation (P2-1/2/3, P3-5):
// deterministic classification of real-clipboard round-trip observations.
// Pure data-in / verdict-out so every failure mode is unit-testable without
// touching the host clipboard (the live round-trip block collects raw Win32
// observations and classifies through this helper). Verdict rules:
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
// advertised format delivered a null handle across the whole bounded window
// while the write stuck, with no readable handle anywhere) or from a write/
// open refusal with no contradictory evidence. Format availability alone
// never proves a round trip and never converts a failure into a skip. The
// confirmed-interception shape is the locked-down-session accommodation the
// campaign evidence describes; on the campaign host the observed lookalike
// was a late-delivery race (first GetClipboardData after SetClipboardData
// returns null; the identical read returns the exact payload milliseconds
// later), which the bounded window absorbs BEFORE classification.

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
    bool hdropAdvertised = false;           // IsClipboardFormatAvailable(CF_HDROP) at probe time
    bool hdropReadable = false;             // GetClipboardData(CF_HDROP) non-null and GlobalLock succeeded
    bool hdropPayloadValid = false;         // the locked payload parsed as a well-formed path list
    uint32_t advertisedFormatCount = 0;     // probe formats enumerated as available
    uint32_t nullHandleFormatCount = 0;     // advertised formats whose GetClipboardData returned null (or failed lock)
    uint32_t readableFormatCount = 0;       // advertised formats that returned a readable handle
    bool independentFormatAdvertised = false; // diagnostic: a non-CF_HDROP probe format (e.g. CF_UNICODETEXT) advertised
    bool independentFormatReadable = false;   // and delivered a readable handle (read by the interception gate)
    bool sequenceChangedSincePublication = false; // GetClipboardSequenceNumber moved between write and read
    // Direction/provenance detail (review-lane B P3-5): kept so the negative
    // controls are observation-distinct even where the verdict collapses.
    bool dropEffectAdvertised = false;      // Preferred DropEffect format present at read time
    bool cutIntentObserved = false;         // cut flag the read actually returned
    bool cutIntentExpected = false;         // cut flag the publication requested
    // Diagnostics (review-lane B P3-6): bounded-window samples before an
    // exact match landed (0 when none did).
    uint32_t attemptsBeforeExact = 0;
};

inline ClipboardRoundTripVerdict ClassifyClipboardRoundTrip(
    const ClipboardRoundTripObservations& observations) {
    if (!observations.writePublished) {
        // review-lane B P2-2: a refused write/open is only an environment
        // limit when the clipboard was genuinely inaccessible. Any readable
        // delivery proves the session allows clipboard access, so the failure
        // is undetermined (possibly a product write-path regression) and must
        // FAIL rather than skip.
        if (observations.hdropReadable || observations.readableFormatCount > 0) {
            return ClipboardRoundTripVerdict::Undetermined;
        }
        return ClipboardRoundTripVerdict::PublicationUnavailable;
    }
    if (observations.pathsMatchExpected && observations.cutIntentMatchesExpected) {
        return ClipboardRoundTripVerdict::Pass;
    }
    // review-lane B P2-1: an interleaved external writer is evaluated BEFORE
    // the interception skip. With the clipboard changing hands between
    // publication and read, nothing observed here can be attributed to the
    // product OR to the documented interception condition, so this case may
    // never silently pass and may never take the skip branch.
    if (observations.sequenceChangedSincePublication) {
        return ClipboardRoundTripVerdict::Undetermined;
    }
    // Mismatch: only CONFIRMED session-level read interception may skip.
    // Confirmation = every advertised format (the product's own write set:
    // CF_HDROP + Preferred DropEffect, plus any third-party formats the probe
    // lists) delivered a null handle across the whole bounded window while
    // the write stuck, and no readable handle exists anywhere
    // (review-lane B P2-3: no synthetic "independent format" witness is
    // required -- the all-advertised-null rule over the probed set is the
    // condition the locked-down-session accommodation exists for).
    const bool everyAdvertisedReadNull = observations.advertisedFormatCount > 0 &&
        observations.nullHandleFormatCount == observations.advertisedFormatCount &&
        observations.readableFormatCount == 0;
    const bool interceptionConfirmed = observations.hdropAdvertised &&
        everyAdvertisedReadNull && !observations.hdropReadable &&
        !observations.independentFormatReadable && !observations.hdropPayloadValid;
    if (interceptionConfirmed) {
        return ClipboardRoundTripVerdict::EnvironmentInterception;
    }
    if (observations.hdropReadable) {
        // The payload was delivered: wrong paths, a malformed list (readable
        // handle, failed parse), or a missing/wrong Preferred DropEffect are
        // product-visible data defects.
        return ClipboardRoundTripVerdict::DataWrongOrIncomplete;
    }
    if (observations.hdropPayloadValid) {
        // Contradictory evidence (payload parsed yet no readable handle and
        // no exact match): undetermined -- never skip, never pass.
        return ClipboardRoundTripVerdict::Undetermined;
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
