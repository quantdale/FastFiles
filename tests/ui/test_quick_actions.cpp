#include "QuickActions.h"

#include <cstring>
#include <iostream>
#include <shellapi.h>
#include <shlobj.h>

#include "ClipboardRoundTripClassifier.h"
#include "../TestSupport.h"

using namespace fftest;

namespace {

// Builds the same DROPFILES + double-null path list shape the shell publishes
// for CF_HDROP, so the lock-read helper can be exercised without the clipboard.
HGLOBAL MakeDropHandle(const std::vector<std::wstring>& paths) {
    size_t characters = 1; // Double-null terminator.
    for (const auto& path : paths) characters += path.size() + 1;
    const size_t bytes = sizeof(DROPFILES) + characters * sizeof(wchar_t);
    HGLOBAL storage = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
    if (storage == nullptr) return nullptr;
    auto* drop = static_cast<DROPFILES*>(GlobalLock(storage));
    if (drop == nullptr) {
        GlobalFree(storage);
        return nullptr;
    }
    drop->pFiles = sizeof(DROPFILES);
    drop->fWide = TRUE;
    auto* output = reinterpret_cast<wchar_t*>(reinterpret_cast<uint8_t*>(drop) + sizeof(DROPFILES));
    for (const auto& path : paths) {
        std::memcpy(output, path.c_str(), path.size() * sizeof(wchar_t));
        output += path.size();
        *output++ = L'\0';
    }
    *output = L'\0';
    GlobalUnlock(storage);
    return storage;
}

HGLOBAL MakeDropEffectHandle(DWORD effect) {
    HGLOBAL storage = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
    if (storage == nullptr) return nullptr;
    auto* value = static_cast<DWORD*>(GlobalLock(storage));
    if (value == nullptr) {
        GlobalFree(storage);
        return nullptr;
    }
    *value = effect;
    GlobalUnlock(storage);
    return storage;
}

} // namespace

int main() {
    const std::vector<std::wstring> published{L"C:\\Fixture\\Moved.txt", L"D:\\Fixture\\Other.txt"};
    const std::vector<std::wstring> shellCaseVariant{L"c:\\fixture\\moved.txt", L"d:\\fixture\\other.txt"};
    const std::vector<std::wstring> changed{L"C:\\Fixture\\Changed.txt", L"D:\\Fixture\\Other.txt"};

    Check(ffui::CanUseLocalCutClipboardFallback(shellCaseVariant, published, false, true, 42, 42),
          "same-sequence case-insensitive paths preserve the app's cut intent");
    Check(!ffui::CanUseLocalCutClipboardFallback(published, published, false, true, 43, 42),
          "a later clipboard publication cannot inherit the previous cut intent");
    Check(!ffui::CanUseLocalCutClipboardFallback(changed, published, false, true, 42, 42),
          "a changed path list cannot inherit the previous cut intent");
    Check(!ffui::CanUseLocalCutClipboardFallback(published, published, true, true, 42, 42),
          "an explicit external cut does not use the app-local fallback");
    Check(!ffui::CanUseLocalCutClipboardFallback(published, published, false, false, 42, 42),
          "a copy publication never becomes a cut through the fallback");
    Check(!ffui::CanUseLocalCutClipboardFallback(published, published, false, true, 0, 0),
          "an unavailable clipboard sequence cannot prove ownership");

    // lock-clipboard-hdrop-read: the HGLOBAL is only dereferenced through a
    // GlobalLock view, and the Preferred DropEffect handle is locked too.
    {
        const std::vector<std::wstring> expected{L"C:\\Fixture\\One.txt", L"D:\\Fixture\\Two.txt"};
        HGLOBAL drop = MakeDropHandle(expected);
        HGLOBAL effect = MakeDropEffectHandle(DROPEFFECT_MOVE);
        Check(drop != nullptr && effect != nullptr, "could not build lock-read fixture");
        std::vector<std::wstring> paths;
        bool cut = false;
        Check(ffui::ReadDropHandle(drop, effect, paths, cut), "locked HDROP read failed");
        Check(paths == expected, "locked HDROP read returned the wrong paths");
        Check(cut, "locked Preferred DropEffect read lost the cut intent");
        if (drop != nullptr) GlobalFree(drop);
        if (effect != nullptr) GlobalFree(effect);
    }

    // A handle that cannot be locked yields an empty list, never a stale one
    // and never a dereference of an unlocked pointer.
    {
        std::vector<std::wstring> paths{L"stale"};
        bool cut = true;
        Check(!ffui::ReadDropHandle(nullptr, nullptr, paths, cut),
              "a null HDROP must report lock failure");
        Check(paths.empty() && !cut, "a failed lock must yield an empty path list");
    }

    // Round-trip through the real clipboard APIs. Classification is explicit
    // (ClipboardRoundTripClassifier): an exact round trip PASSes; DELIVERED
    // but wrong or incomplete data FAILs immediately (never retried away);
    // only CONFIRMED conditions (every advertised format -- independent
    // formats included -- delivering nothing across a bounded read window
    // while the write stuck, or a refused write/open) are loud SKIPs.
    // review-remediation P2-B. The bounded window exists because raw-probe
    // evidence (build/campaign/probe_clipboard) shows this host can deliver
    // freshly written formats LATE: the first read-after-write sees null
    // handles while the identical read milliseconds later returns the exact
    // payload. That race must not masquerade as either data loss or
    // "interception", and format availability alone must never pass a round
    // trip.
    {
        const std::vector<std::wstring> expected{L"C:\\Fixture\\RoundTrip.txt", L"D:\\Fixture\\RoundTrip2.txt"};
        if (!ffui::CopyFilesToClipboard(nullptr, expected, true)) {
            std::cout << "clipboard unavailable (write refused); "
                         "SKIP real-clipboard round-trip ("
                      << ClipboardRoundTripVerdictName(fftest::ClipboardRoundTripVerdict::PublicationUnavailable)
                      << ")\n";
        } else {
            const DWORD sequenceAtWrite = GetClipboardSequenceNumber();
            fftest::ClipboardRoundTripObservations observations;
            observations.writePublished = true;
            bool delivered = false;
            bool exact = false;
            for (int attempt = 0; attempt < 80 && !exact; ++attempt) {
                bool cut = false;
                const auto read = ffui::ReadFilesFromClipboard(nullptr, cut);
                if (!read.empty() || cut) {
                    // Something was delivered. Record it, but keep sampling:
                    // this host delivers the published formats with SKEW
                    // (observed under CTest: CF_HDROP exact while the
                    // Preferred DropEffect had not landed yet), so an
                    // incomplete first delivery may complete inside the
                    // window. Only an EXACT match (paths AND cut intent) ever
                    // passes; a persistent wrong delivery still FAILs at
                    // window exhaustion and is never skipped away.
                    observations.pathsMatchExpected = (read == expected);
                    observations.cutIntentMatchesExpected = (cut == true); // published cut=true
                    delivered = true;
                    exact = observations.pathsMatchExpected && observations.cutIntentMatchesExpected;
                }
                if (!exact) Sleep(25);
            }
            observations.sequenceChangedSincePublication =
                GetClipboardSequenceNumber() != sequenceAtWrite;
            if (!delivered) {
                // Nothing was delivered across the whole window: classify
                // from raw observations (advertised formats, actual
                // GetClipboardData handles, lockability, parsed payload).
                if (OpenClipboard(nullptr)) {
                    const UINT probeFormats[] = {CF_HDROP, CF_UNICODETEXT, CF_TEXT, CF_DIB};
                    for (const UINT format : probeFormats) {
                        if (!IsClipboardFormatAvailable(format)) continue;
                        ++observations.advertisedFormatCount;
                        if (format == CF_UNICODETEXT) observations.independentFormatAdvertised = true;
                        HANDLE data = GetClipboardData(format);
                        if (data == nullptr) {
                            ++observations.nullHandleFormatCount;
                            continue;
                        }
                        void* locked = GlobalLock(data);
                        if (locked == nullptr) {
                            ++observations.nullHandleFormatCount;
                            continue;
                        }
                        ++observations.readableFormatCount;
                        if (format == CF_HDROP) {
                            observations.hdropAdvertised = true;
                            observations.hdropReadable = true;
                            std::vector<std::wstring> probePaths;
                            bool probeCut = false;
                            observations.hdropPayloadValid =
                                ffui::ReadDropHandle(data, nullptr, probePaths, probeCut) && !probePaths.empty();
                        }
                        if (format == CF_UNICODETEXT) observations.independentFormatReadable = true;
                        GlobalUnlock(data);
                    }
                    CloseClipboard();
                }
                if (IsClipboardFormatAvailable(CF_HDROP)) observations.hdropAdvertised = true;
            }
            const auto verdict = fftest::ClassifyClipboardRoundTrip(observations);
            if (verdict == fftest::ClipboardRoundTripVerdict::Pass) {
                Check(true, "clipboard HDROP round-trip preserved the exact paths");
                Check(true, "clipboard Preferred DropEffect round-trip preserved the cut intent");
            } else if (verdict == fftest::ClipboardRoundTripVerdict::EnvironmentInterception) {
                // The documented locked-down-session condition, confirmed
                // from actual null handles across ALL advertised formats for
                // the whole bounded window -- not from format availability.
                std::cout << "clipboard read-back delivered nothing across the bounded window "
                             "(every advertised format has a null handle while the write stuck); "
                             "SKIP real-clipboard round-trip ("
                          << ClipboardRoundTripVerdictName(verdict) << ")\n";
            } else if (verdict == fftest::ClipboardRoundTripVerdict::PublicationUnavailable) {
                std::cout << "clipboard became unavailable; SKIP real-clipboard round-trip ("
                          << ClipboardRoundTripVerdictName(verdict) << ")\n";
            } else {
                std::cout << "clipboard round-trip classified " << ClipboardRoundTripVerdictName(verdict)
                          << " delivered=" << (delivered ? 1 : 0)
                          << " pathsMatch=" << (observations.pathsMatchExpected ? 1 : 0)
                          << " cutMatch=" << (observations.cutIntentMatchesExpected ? 1 : 0)
                          << " hdropReadable=" << (observations.hdropReadable ? 1 : 0)
                          << " payloadValid=" << (observations.hdropPayloadValid ? 1 : 0)
                          << " advertised=" << observations.advertisedFormatCount
                          << " null=" << observations.nullHandleFormatCount
                          << " readable=" << observations.readableFormatCount << "\n";
                Check(false, "clipboard round-trip returned wrong or incomplete data (classified FAIL, not skipped)");
            }
            if (OpenClipboard(nullptr)) {
                EmptyClipboard();
                CloseClipboard();
            }
        }
    }

    return FailureCount();
}
