#include <filesystem>
#include <fstream>

#include "ffsearch/History.h"
#include "../TestSupport.h"

using namespace fftest;

namespace {

const std::filesystem::path TempDir() {
    return std::filesystem::temp_directory_path() / L"ffsearch_history_tests";
}

} // namespace

int main() {
    const auto dir = TempDir();
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto path = dir / L"history.txt";

    // Case-insensitive dedup on record (matches the search's own
    // case-insensitive matching semantics).
    {
        ffsearch::SearchHistory history;
        Check(history.Record(L"abc", path), "the first record succeeds");
        history.Record(L"ABC", path);
        Check(history.Queries().size() == 1 && history.Queries().front() == L"ABC",
              "recording a case variant dedups instead of adding a second entry");
        history.Record(L"report", path);
        Check(history.Queries().size() == 2 && history.Queries().front() == L"report",
              "a new query moves to the front of the history");
    }
    {
        ffsearch::SearchHistory reloaded;
        Check(reloaded.Load(path), "the persisted history loads");
        Check(reloaded.Queries().size() == 2, "reload preserves the deduped entries");
    }

    // Load dedups case variants already present in a legacy file.
    {
        std::ofstream file(path, std::ios::trunc | std::ios::binary);
        file << "100\tReport\n90\treport\n80\tREPORT\n70\tunrelated\n";
    }
    {
        ffsearch::SearchHistory loaded;
        loaded.Load(path);
        Check(loaded.Queries().size() == 2 &&
                  loaded.Queries()[0] == L"Report" && loaded.Queries()[1] == L"unrelated",
              "loading dedups legacy case-variant lines, keeping the newest spelling");
    }

    // RecordQuery mutates memory only; the disk flush is the caller's decision
    // so a UI thread can coalesce writes.
    std::filesystem::remove(path);
    {
        ffsearch::SearchHistory deferred;
        deferred.RecordQuery(L"in memory only");
        Check(deferred.Queries().size() == 1 && !std::filesystem::exists(path),
              "RecordQuery defers the disk flush");
        Check(deferred.Save(path) && std::filesystem::exists(path),
              "an explicit Save persists the deferred entries");
    }

    // Recording disabled records nothing and never touches the file.
    {
        ffsearch::SearchHistory disabled(false);
        Check(disabled.Record(L"quiet", path), "recording disabled reports success without writing");
        Check(disabled.Queries().empty(), "recording disabled keeps the store empty");
    }

    std::filesystem::remove_all(dir);
    return fftest::FailureCount() == 0 ? 0 : 1;
}
