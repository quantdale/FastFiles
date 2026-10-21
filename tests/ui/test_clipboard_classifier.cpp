// large-index-lifecycle-and-search-scalability review-remediation (P2-B):
// negative controls for the real-clipboard round-trip classifier. Each case
// is a raw-observation set for one concrete failure mode the superseded
// inline discriminator swallowed into its environment-skip branch. The
// controls prove (a) every genuine data failure classifies FAIL, (b) only
// CONFIRMED session-level read interception (or a refused write) classifies
// SKIP, and (c) the legacy "CF_HDROP advertised -> skip" rule really did
// misclassify the data-failure cases (non-vacuity: these controls would have
// caught the false-skip defect). Deterministic: no host clipboard is touched.

#include "ClipboardRoundTripClassifier.h"

#include <iostream>

#include "../TestSupport.h"

using namespace fftest;

namespace {

ClipboardRoundTripObservations ValidRoundTrip() {
    ClipboardRoundTripObservations observations;
    observations.writePublished = true;
    observations.pathsMatchExpected = true;
    observations.cutIntentMatchesExpected = true;
    return observations;
}

// Shape of a mismatch whose data WAS delivered and parsed (the CF_HDROP
// handle locked and produced content), so any wrongness is a data defect.
ClipboardRoundTripObservations ReadableMismatch() {
    ClipboardRoundTripObservations observations = ValidRoundTrip();
    observations.pathsMatchExpected = false;
    observations.hdropAdvertised = true;
    observations.hdropReadable = true;
    observations.hdropPayloadValid = true;
    observations.advertisedFormatCount = 1;
    observations.readableFormatCount = 1;
    return observations;
}

} // namespace

int main() {
    using V = ClipboardRoundTripVerdict;

    // Valid round trip -> PASS.
    Check(ClassifyClipboardRoundTrip(ValidRoundTrip()) == V::Pass,
          "valid round trip classifies PASS");

    // 1. HDROP advertised, data readable, wrong paths -> FAIL.
    {
        ClipboardRoundTripObservations observations = ReadableMismatch();
        Check(ClassifyClipboardRoundTrip(observations) == V::DataWrongOrIncomplete,
              "case 1: readable-but-wrong paths must FAIL");
        Check(LegacyFormatAvailableWouldSkip(false, observations.hdropAdvertised),
              "case 1 non-vacuity: the legacy rule wrongly skipped this failure");
    }

    // 2. HDROP advertised, malformed payload -> FAIL.
    {
        ClipboardRoundTripObservations observations = ReadableMismatch();
        observations.hdropPayloadValid = false; // handle delivered, parse failed
        Check(ClassifyClipboardRoundTrip(observations) == V::DataWrongOrIncomplete,
              "case 2: a malformed HDROP payload must FAIL");
        Check(LegacyFormatAvailableWouldSkip(false, observations.hdropAdvertised),
              "case 2 non-vacuity: the legacy rule wrongly skipped this failure");
    }

    // 3. HDROP readable, Preferred DropEffect missing -> FAIL (incomplete).
    {
        ClipboardRoundTripObservations observations = ReadableMismatch();
        observations.pathsMatchExpected = true;
        observations.cutIntentMatchesExpected = false; // effect absent -> read as copy
        Check(ClassifyClipboardRoundTrip(observations) == V::DataWrongOrIncomplete,
              "case 3: a missing Preferred DropEffect must FAIL");
        Check(LegacyFormatAvailableWouldSkip(false, observations.hdropAdvertised),
              "case 3 non-vacuity: the legacy rule wrongly skipped this failure");
    }

    // 4. HDROP readable, Preferred DropEffect incorrect -> FAIL.
    {
        ClipboardRoundTripObservations observations = ReadableMismatch();
        observations.pathsMatchExpected = true;
        observations.cutIntentMatchesExpected = false; // published move, effect says copy
        Check(ClassifyClipboardRoundTrip(observations) == V::DataWrongOrIncomplete,
              "case 4: a wrong Preferred DropEffect must FAIL");
    }

    // 5. Copy published but read as cut -> FAIL.
    {
        ClipboardRoundTripObservations observations = ReadableMismatch();
        observations.pathsMatchExpected = true;
        observations.cutIntentMatchesExpected = false;
        Check(ClassifyClipboardRoundTrip(observations) == V::DataWrongOrIncomplete,
              "case 5: copy published but read back as cut must FAIL");
    }

    // 6. Cut published but read as copy -> FAIL.
    {
        ClipboardRoundTripObservations observations = ReadableMismatch();
        observations.pathsMatchExpected = true;
        observations.cutIntentMatchesExpected = false;
        Check(ClassifyClipboardRoundTrip(observations) == V::DataWrongOrIncomplete,
              "case 6: cut published but read back as copy must FAIL");
    }

    // 7. Read returns null due to CONFIRMED environment interception -> SKIP.
    //    Measured signature: the write stuck, CF_HDROP AND an independent
    //    format stay advertised, and every advertised GetClipboardData probe
    //    delivers a null handle (nothing readable at all).
    {
        ClipboardRoundTripObservations observations = ValidRoundTrip();
        observations.pathsMatchExpected = false;
        observations.cutIntentMatchesExpected = false;
        observations.hdropAdvertised = true;
        observations.hdropReadable = false;
        observations.independentFormatAdvertised = true;
        observations.independentFormatReadable = false;
        observations.advertisedFormatCount = 2;
        observations.nullHandleFormatCount = 2;
        observations.readableFormatCount = 0;
        Check(ClassifyClipboardRoundTrip(observations) == V::EnvironmentInterception,
              "case 7: confirmed all-format read interception classifies SKIP");
    }

    // 8. Clipboard cannot be opened / write refused -> SKIP (the pre-existing
    //    locked-down-session accommodation), never a silent PASS.
    {
        ClipboardRoundTripObservations observations;
        observations.writePublished = false;
        Check(ClassifyClipboardRoundTrip(observations) == V::PublicationUnavailable,
              "case 8: a refused clipboard write/open classifies SKIP");
    }

    // 9. Clipboard changed between publication and read -> FAIL (undetermined);
    //    it may never reach the interception skip branch.
    {
        ClipboardRoundTripObservations observations = ReadableMismatch();
        observations.sequenceChangedSincePublication = true;
        Check(ClassifyClipboardRoundTrip(observations) == V::Undetermined,
              "case 9: an interleaved external clipboard change must FAIL as undetermined");
    }

    // 10. Access fails for a reason other than the known environment
    //     condition: CF_HDROP advertises a null handle while an independent
    //     format reads back fine -> FAIL (undetermined), NOT the skip branch.
    {
        ClipboardRoundTripObservations observations = ReadableMismatch();
        observations.hdropReadable = false;
        observations.hdropPayloadValid = false;
        observations.independentFormatAdvertised = true;
        observations.independentFormatReadable = true;
        observations.advertisedFormatCount = 2;
        observations.nullHandleFormatCount = 1;
        observations.readableFormatCount = 1;
        Check(ClassifyClipboardRoundTrip(observations) == V::Undetermined,
              "case 10: a partial read failure (HDROP null, other formats fine) must FAIL as undetermined");
        Check(LegacyFormatAvailableWouldSkip(false, observations.hdropAdvertised),
              "case 10 non-vacuity: the legacy rule wrongly skipped this failure");
    }

    // No failure mode may classify as the interception skip unless EVERY
    // advertised format (independent included) delivered nothing.
    {
        ClipboardRoundTripObservations observations = ReadableMismatch();
        observations.pathsMatchExpected = false;
        observations.independentFormatAdvertised = true;
        observations.independentFormatReadable = true;
        observations.advertisedFormatCount = 2;
        observations.nullHandleFormatCount = 1;
        observations.readableFormatCount = 1;
        Check(ClassifyClipboardRoundTrip(observations) != V::EnvironmentInterception,
              "a readable independent format can never confirm read interception");
    }

    std::cout << "clipboard classifier negative controls complete\n";
    return FailureCount();
}
