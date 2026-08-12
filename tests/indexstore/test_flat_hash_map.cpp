#include <cstddef>
#include <string>
#include <stdexcept>

#include "ffindexstore/FlatHashMap.h"
#include "../TestSupport.h"

using namespace fftest;

namespace {

void TestOperatorBracketGrowsBeforeFullCapacity() {
    ffindexstore::FlatHashMap<int, int> map;
    for (int key = 0; key < 128; ++key) {
        map[key] = key * 2;
    }

    Check(map.size() == 128, "operator[] retains every key across growth");
    Check(map.bucket_count() > map.size(), "operator[] grows before reaching full capacity");
    Check(map.find(127) != nullptr && *map.find(127) == 254,
          "operator[] preserves the final value after repeated rehashes");
}

struct ConstantHash {
    size_t operator()(int) const noexcept { return 0; }
};

struct DeferredLowBitHash {
    size_t operator()(int key) const noexcept {
        return static_cast<size_t>(key) << 7;
    }
};

void TestProbeBoundTriggersBoundedGrowth() {
    ffindexstore::FlatHashMap<int, int, DeferredLowBitHash> map;
    for (int key = 0; key < 80; ++key) {
        map.insert(key, key * 3);
    }

    Check(map.size() == 80, "a long valid probe run is recovered by growing the table");
    Check(map.bucket_count() >= 256, "probe-bound recovery grows to a table with usable low bits");
    Check(map.find(79) != nullptr && *map.find(79) == 237,
          "probe-bound recovery preserves the inserted value");
}

void TestProbeDistanceAbortsInsertion() {
    ffindexstore::FlatHashMap<int, int, ConstantHash> map;
    bool threw = false;
    try {
        for (int key = 0; key < 80; ++key) {
            map.insert(key, key);
        }
    } catch (const std::runtime_error& error) {
        threw = std::string(error.what()).find("64") != std::string::npos;
    }

    Check(threw, "insertion aborts with an error after the maximum probe distance");
    Check(map.size() == ffindexstore::FlatHashMap<int, int, ConstantHash>::kMaxProbeDistance,
          "probe abort leaves the successfully inserted keys intact");
    Check(map.find(63) != nullptr && *map.find(63) == 63,
          "the last key before probe abort remains readable");
}

} // namespace

int main() {
    TestOperatorBracketGrowsBeforeFullCapacity();
    TestProbeBoundTriggersBoundedGrowth();
    TestProbeDistanceAbortsInsertion();

    if (fftest::FailureCount() > 0) {
        std::fprintf(stderr, "%d test(s) failed\n", fftest::FailureCount());
        return 1;
    }
    std::printf("All tests passed.\n");
    return 0;
}
