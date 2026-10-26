#include "ffui/AsyncWorkAdmission.h"
#include "../TestSupport.h"

int main() {
    std::vector<std::shared_future<void>> work;
    fftest::Check(!ffui::AdmitAsyncWork(work, 0), "zero capacity declines work");
    fftest::Check(ffui::AdmitAsyncWork(work, 2), "empty list admits work");
    std::promise<void> release;
    const auto gate = release.get_future().share();
    work.push_back(std::async(std::launch::async, [gate] { gate.wait(); }).share());
    fftest::Check(ffui::AdmitAsyncWork(work, 2), "capacity remains for second task");
    work.push_back(std::async(std::launch::async, [gate] { gate.wait(); }).share());
    // Both async states are unfinished until this thread explicitly releases
    // them. Removing either final state here would join and deadlock the test.
    fftest::Check(!ffui::AdmitAsyncWork(work, 2), "saturation declines new work without releasing unfinished states");
    fftest::Check(work.size() == 2, "all unfinished states remain owned");
    release.set_value();
    for (const auto& item : work) item.wait();
    fftest::Check(ffui::AdmitAsyncWork(work, 2), "completed work reopens admission");
    fftest::Check(work.empty(), "completed states are reclaimed");
    return fftest::FailureCount() == 0 ? 0 : 1;
}
