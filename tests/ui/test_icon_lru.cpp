#include "ffui/IconLru.h"

#include <cstdlib>

#include "../TestSupport.h"

namespace {

void TestPromotionAndBoundedEviction() {
    ffui::IconLru lru;
    lru.Touch(L".alpha");
    lru.Touch(L".beta");
    lru.Touch(L".alpha");

    const auto evicted = lru.TrimTo(1);
    fftest::Check(evicted.size() == 1 && evicted.front() == L".beta",
                  "recently touched icon key survives bounded eviction");
    fftest::Check(lru.Size() == 1, "LRU trim keeps the requested maximum");
}

void TestClearResetsState() {
    ffui::IconLru lru;
    lru.Touch(L"folder");
    lru.Clear();
    fftest::Check(lru.Size() == 0, "LRU clear removes every key");
    lru.Touch(L".after-clear");
    const auto evicted = lru.TrimTo(0);
    fftest::Check(evicted.size() == 1 && evicted.front() == L".after-clear",
                  "LRU remains usable after clear");
}

} // namespace

int main() {
    TestPromotionAndBoundedEviction();
    TestClearResetsState();
    return fftest::FailureCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
