// large-index-lifecycle-and-search-scalability review-remediation (P2-B),
// with adversarial-review lane-B remediation:
// negative controls for the real-clipboard round-trip classifier. Each case
// is a raw-observation set for one concrete failure mode the superseded
// inline discriminator swallowed into its environment-skip branch. The
// controls prove (a) every genuine data failure classifies FAIL, (b) only
// CONFIRMED session-level read interception (or a refused write with no
// contradicting evidence) classifies SKIP, and (c) the legacy
// "CF_HDROP advertised -> skip" rule really did misclassify the data-failure
// cases (non-vacuity: these controls would have caught the false-skip
// defect). Deterministic: no host clipboard is touched.

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
    observations.cutIntentExpected = true;
    observations.cutIntentObserved = true;
    observations.attemptsBeforeExact = 1;
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
    observations.attemptsBeforeExact = 0;
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

    // 2. HDROP advertised, malformed payload -> FAIL (lane B P3-5b: the
    //    payload-validity field is load-bearing for this classification).
    {
        ClipboardRoundTripObservations observations = ReadableMismatch();
        observations.hdropPayloadValid = false; // handle delivered, parse failed
        Check(ClassifyClipboardRoundTrip(observations) == V::DataWrongOrIncomplete,
              "case 2: a malformed HDROP payload must FAIL");
        Check(LegacyFormatAvailableWouldSkip(false, observations.hdropAdvertised),
              "case 2 non-vacuity: the legacy rule wrongly skipped this failure");
    }

    // 2b. Contradictory evidence (payload parsed, nothing readable, no exact
    //     match) -> FAIL(undetermined), never a skip.
    {
        ClipboardRoundTripObservations observations = ValidRoundTrip();
        observations.pathsMatchExpected = false;
        observations.cutIntentMatchesExpected = false;
        observations.hdropAdvertised = true;
        observations.hdropPayloadValid = true;
        observations.hdropReadable = false;
        observations.advertisedFormatCount = 1;
        observations.nullHandleFormatCount = 1;
        Check(ClassifyClipboardRoundTrip(observations) == V::Undetermined,
              "case 2b: contradictory probe evidence must FAIL as undetermined");
    }

    // 3. HDROP readable, Preferred DropEffect MISSING -> FAIL (incomplete).
    //    Distinct observation: effect format absent at read time.
    {
        ClipboardRoundTripObservations observations = ReadableMismatch();
        observations.pathsMatchExpected = true;
        observations.cutIntentMatchesExpected = false;
        observations.dropEffectAdvertised = false;
        observations.cutIntentObserved = false;
        observations.cutIntentExpected = true;
        Check(ClassifyClipboardRoundTrip(observations) == V::DataWrongOrIncomplete,
              "case 3: a missing Preferred DropEffect must FAIL");
        Check(!observations.dropEffectAdvertised,
              "case 3 observation is distinct: the effect format is absent");
        Check(LegacyFormatAvailableWouldSkip(false, observations.hdropAdvertised),
              "case 3 non-vacuity: the legacy rule wrongly skipped this failure");
    }

    // 4. HDROP readable, Preferred DropEffect INCORRECT -> FAIL. Distinct
    //    observation: effect format present but the intent value is wrong.
    {
        ClipboardRoundTripObservations observations = ReadableMismatch();
        observations.pathsMatchExpected = true;
        observations.cutIntentMatchesExpected = false;
        observations.dropEffectAdvertised = true;
        observations.cutIntentObserved = false;
        observations.cutIntentExpected = true;
        Check(ClassifyClipboardRoundTrip(observations) == V::DataWrongOrIncomplete,
              "case 4: a wrong Preferred DropEffect must FAIL");
        Check(observations.dropEffectAdvertised && !observations.cutIntentObserved,
              "case 4 observation is distinct: effect present with the wrong value");
        Check(LegacyFormatAvailableWouldSkip(false, observations.hdropAdvertised),
              "case 4 non-vacuity: the legacy rule wrongly skipped this failure");
    }

    // 5. Copy published but read as cut -> FAIL. Distinct observation:
    //    expected cut=false, observed cut=true.
    {
        ClipboardRoundTripObservations observations = ReadableMismatch();
        observations.pathsMatchExpected = true;
        observations.cutIntentMatchesExpected = false;
        observations.dropEffectAdvertised = true;
        observations.cutIntentExpected = false;
        observations.cutIntentObserved = true;
        Check(ClassifyClipboardRoundTrip(observations) == V::DataWrongOrIncomplete,
              "case 5: copy published but read back as cut must FAIL");
    }

    // 6. Cut published but read as copy -> FAIL. Distinct observation:
    //    expected cut=true, observed cut=false with the effect advertised.
    {
        ClipboardRoundTripObservations observations = ReadableMismatch();
        observations.pathsMatchExpected = true;
        observations.cutIntentMatchesExpected = false;
        observations.dropEffectAdvertised = true;
        observations.cutIntentExpected = true;
        observations.cutIntentObserved = false;
        Check(ClassifyClipboardRoundTrip(observations) == V::DataWrongOrIncomplete,
              "case 6: cut published but read back as copy must FAIL");
    }

    // 7. Read returns null due to CONFIRMED environment interception -> SKIP.
    //    The product's own write set (CF_HDROP + Preferred DropEffect) stays
    //    advertised while every advertised format delivers a null handle for
    //    the whole bounded window (lane B P2-3: no synthetic independent-format
    //    witness required).
    {
        ClipboardRoundTripObservations observations = ValidRoundTrip();
        observations.pathsMatchExpected = false;
        observations.cutIntentMatchesExpected = false;
        observations.hdropAdvertised = true;
        observations.hdropReadable = false;
        observations.dropEffectAdvertised = true;
        observations.advertisedFormatCount = 2;
        observations.nullHandleFormatCount = 2;
        observations.readableFormatCount = 0;
        Check(ClassifyClipboardRoundTrip(observations) == V::EnvironmentInterception,
              "case 7: confirmed all-format read interception classifies SKIP");
    }

    // 7b. review-lane B P2-1 counterexample: the interception shape PLUS an
    //     interleaved external clipboard change must NOT take the skip branch.
    {
        ClipboardRoundTripObservations observations = ValidRoundTrip();
        observations.pathsMatchExpected = false;
        observations.cutIntentMatchesExpected = false;
        observations.hdropAdvertised = true;
        observations.hdropReadable = false;
        observations.advertisedFormatCount = 2;
        observations.nullHandleFormatCount = 2;
        observations.readableFormatCount = 0;
        observations.sequenceChangedSincePublication = true;
        Check(ClassifyClipboardRoundTrip(observations) == V::Undetermined,
              "case 7b: interception-shaped probes after an external change must FAIL as undetermined");
    }

    // 8. Clipboard cannot be opened / write refused -> SKIP (the pre-existing
    //    locked-down-session accommodation), never a silent PASS.
    {
        ClipboardRoundTripObservations observations;
        observations.writePublished = false;
        Check(ClassifyClipboardRoundTrip(observations) == V::PublicationUnavailable,
              "case 8: a refused clipboard write/open classifies SKIP");
    }

    // 8b. review-lane B P2-2 counterexample: a failed publication with
    //     readable clipboard evidence is NOT an environment limit -> FAIL.
    {
        ClipboardRoundTripObservations observations;
        observations.writePublished = false;
        observations.hdropAdvertised = true;
        observations.hdropReadable = true;
        observations.advertisedFormatCount = 1;
        observations.readableFormatCount = 1;
        Check(ClassifyClipboardRoundTrip(observations) == V::Undetermined,
              "case 8b: a failed write with readable evidence must FAIL as undetermined");
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
