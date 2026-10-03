// performance-closure-and-multisurface-acceleration 7.3: bounded top-N
// admission property test.
//
// The admission may only change WHETHER a hopeless row is materialized, never
// WHICH candidates are accepted. For every sort field and direction, gating
// Add on AdmitView (skipping when it reports the row cannot enter the bounded
// set) must produce exactly the reference accumulator's results, and
// AddViewMatched (the caller already evaluated the query predicates on the
// view) must produce exactly the same results as Add.

#include "ffsearch/Search.h"
#include "../TestSupport.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace fftest;

namespace {

std::u16string ToUtf16(const std::wstring& value) {
    return std::u16string(value.begin(), value.end());
}

struct Row {
    std::wstring name;
    uint64_t sizeBytes = 0;
    uint64_t modifiedTime = 0;
    uint64_t createdTime = 0;
    uint64_t allocatedSizeBytes = 0;
    bool allocatedSizeKnown = false;
    int64_t volumeId = 1;
    uint64_t id = 0;
};

std::vector<Row> BuildRows() {
    std::vector<Row> rows;
    for (uint64_t index = 0; index < 300; ++index) {
        Row row;
        const uint64_t shape = index % 4;
        if (shape == 0) {
            row.name = L"m" + std::to_wstring(index) + L"_common.txt";
        } else if (shape == 1) {
            row.name = L"m_" + std::wstring(index % 13, L'x') + L".dat";
        } else if (shape == 2) {
            row.name = L"common_" + std::to_wstring(index) + L"_m.bin";
        } else {
            row.name = L"M" + std::to_wstring(index) + L".txt";  // mixed-case m
        }
        row.sizeBytes = (index * 7919) % 100000;
        row.modifiedTime = 1000000 + (index * 104729) % 500000;
        row.createdTime = 500000 + (index * 1299709) % 400000;
        row.allocatedSizeBytes = row.sizeBytes + 4096;
        row.allocatedSizeKnown = (index % 3) != 0;
        row.id = index + 1;
        rows.push_back(row);
    }
    return rows;
}

ffsearch::Candidate MakeCandidate(const Row& row) {
    ffsearch::Candidate candidate;
    candidate.name = row.name;
    candidate.folder = L"C:\\work";
    candidate.sizeBytes = row.sizeBytes;
    candidate.modifiedTime = row.modifiedTime;
    candidate.isDirectory = false;
    candidate.createdTime = row.createdTime;
    candidate.volumeId = row.volumeId;
    candidate.id = row.id;
    candidate.parentId = 1;
    candidate.persistentIndexed = true;
    candidate.allocatedSizeBytes = row.allocatedSizeBytes;
    candidate.allocatedSizeKnown = row.allocatedSizeKnown;
    return candidate;
}

ffsearch::CandidateView MakeView(const Row& row, std::u16string& nameStorage) {
    nameStorage = ToUtf16(row.name);
    ffsearch::CandidateView view;
    view.name = nameStorage;
    view.folder = std::wstring_view();
    view.sizeBytes = row.sizeBytes;
    view.modifiedTime = row.modifiedTime;
    view.isDirectory = false;
    view.createdTime = row.createdTime;
    view.volumeId = row.volumeId;
    view.id = row.id;
    view.parentId = 1;
    view.persistentIndexed = true;
    view.nameAsciiLower = false;
    view.allocatedSizeBytes = row.allocatedSizeBytes;
    view.allocatedSizeKnown = row.allocatedSizeKnown;
    view.nameIsAscii = true;
    return view;
}

void DumpResults(const char* label, const ffsearch::SearchResponse& response) {
    std::fprintf(stderr, "%s: n=%zu truncated=%d\n", label, response.results.size(),
                 response.truncated ? 1 : 0);
    for (const auto& result : response.results) {
        std::fprintf(stderr, "  id=%llu tier=%d name=%ls\n",
                     static_cast<unsigned long long>(result.candidate.id),
                     static_cast<int>(result.tier), result.candidate.name.c_str());
    }
}

bool SameResults(const ffsearch::SearchResponse& left, const ffsearch::SearchResponse& right) {
    if (left.results.size() != right.results.size()) return false;
    if (left.truncated != right.truncated) return false;
    for (size_t index = 0; index < left.results.size(); ++index) {
        if (left.results[index].candidate.id != right.results[index].candidate.id
            || left.results[index].candidate.name != right.results[index].candidate.name
            || left.results[index].tier != right.results[index].tier) {
            return false;
        }
    }
    return true;
}

}  // namespace

int main() {
    const auto registry = ffsearch::FilterRegistry::WithDefaults();
    const std::vector<Row> rows = BuildRows();
    const ffsearch::SortField fields[] = {
        ffsearch::SortField::Relevance, ffsearch::SortField::Filename, ffsearch::SortField::Path,
        ffsearch::SortField::Size, ffsearch::SortField::Modified, ffsearch::SortField::Created,
        ffsearch::SortField::AllocatedSize,
    };

    for (const ffsearch::SortField field : fields) {
        for (const bool descending : {false, true}) {
            ffsearch::SearchRequest request{ffsearch::ParseQuery(L"m", registry),
                                            ffsearch::SearchScope::AllIndexedLocations, L"C:"};
            request.sortField = field;
            request.descending = descending;
            request.maxResults = 8;
            request.chunkSize = 64;

            ffsearch::SearchAccumulator reference(request);
            ffsearch::SearchAccumulator gated(request);
            ffsearch::SearchAccumulator preMatched(request);
            size_t admittedOut = 0;
            for (const Row& row : rows) {
                std::u16string nameStorage;
                const ffsearch::CandidateView view = MakeView(row, nameStorage);
                if (!request.query.MatchesView(view)) continue;  // mirrors the pipeline hot loop
                const ffsearch::Candidate candidate = MakeCandidate(row);
                reference.Add(candidate);
                if (gated.AdmitView(view)) {
                    gated.Add(candidate);
                } else {
                    ++admittedOut;
                }
                preMatched.AddViewMatched(candidate);
            }

            const ffsearch::SearchResponse referenceResults = reference.Finish();
            const ffsearch::SearchResponse gatedResults = gated.Finish();
            const ffsearch::SearchResponse preMatchedResults = preMatched.Finish();
            if (!SameResults(referenceResults, gatedResults)) {
                std::fprintf(stderr, "field=%d descending=%d admittedOut=%zu\n",
                             static_cast<int>(field), descending ? 1 : 0, admittedOut);
                DumpResults("reference", referenceResults);
                DumpResults("gated", gatedResults);
            }
            Check(SameResults(referenceResults, gatedResults),
                  "AdmitView-gated adds produce exactly the reference bounded result set");
            Check(SameResults(referenceResults, preMatchedResults),
                  "AddViewMatched produces exactly the reference bounded result set");
            if (field != ffsearch::SortField::Path) {
                Check(admittedOut > 0, "the admission actually admitted rows out for this sort");
            } else {
                Check(admittedOut == 0, "Path sort never admits rows out (folder unavailable)");
            }
        }
    }

    // Unbounded requests never admit rows out: the set must be complete.
    {
        ffsearch::SearchRequest request{ffsearch::ParseQuery(L"m", registry),
                                        ffsearch::SearchScope::AllIndexedLocations, L"C:"};
        request.sortField = ffsearch::SortField::Filename;
        request.maxResults = 0;
        ffsearch::SearchAccumulator reference(request);
        ffsearch::SearchAccumulator gated(request);
        for (const Row& row : rows) {
            std::u16string nameStorage;
            const ffsearch::CandidateView view = MakeView(row, nameStorage);
            if (!request.query.MatchesView(view)) continue;
            const ffsearch::Candidate candidate = MakeCandidate(row);
            reference.Add(candidate);
            Check(gated.AdmitView(view), "unbounded requests always admit the view");
            gated.Add(candidate);
        }
        Check(SameResults(reference.Finish(), gated.Finish()),
              "unbounded request results are identical with the admission in place");
    }

    return FailureCount() == 0 ? 0 : 1;
}
