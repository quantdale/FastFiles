#pragma once

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>
#include <windows.h>

#include "EngineClient.h"
#include "ffsearch/Search.h"
#include "ffsearch/History.h"

namespace ffui {

constexpr UINT WM_APP_SEARCH_COMPLETE = WM_APP + 6;

class SearchPanel {
public:
    SearchPanel() = default;
    ~SearchPanel();
    bool Initialize(HWND owner, EngineClient* engine,
                    std::function<void(const ffsearch::Candidate&, const ffsearch::PathReconstruction&)> navigate,
                    bool retainHistory);
    void ShowAndFocus(const std::wstring& currentPath, bool engineActive);
    void Hide();
    bool Visible() const { return visible_; }
    void Reposition();
    bool HandleOwnerCommand(WPARAM wParam, LPARAM lParam);
    bool HandleNotify(LPARAM lParam);
    bool HandleDrawItem(LPARAM lParam);
    bool HandleTimer(UINT_PTR timerId);
    bool HandleCompletion(LPARAM lParam);
    void SetEngineActive(bool active);
    void ClearHistory();

private:
    struct Work {
        uint64_t generation = 0;
        std::vector<ffsearch::Candidate> candidates;
        ffsearch::SearchRequest request;
    };
    struct Completion {
        uint64_t generation = 0;
        ffsearch::SearchResponse response;
        std::vector<ffsearch::Candidate> candidates;
    };

    void WorkerMain();
    void Dispatch();
    void PopulateCandidates(std::vector<ffsearch::Candidate>& candidates) const;
    void UpdateScopeAvailability(bool showFallbackNotice);
    void UpdateStatus(const std::wstring& text);
    void LoadHistory();
    void SaveHistory() const;
    void RecordHistory(const std::wstring& query);
    static std::filesystem::path HistoryPath();

    HWND owner_ = nullptr;
    HWND query_ = nullptr;
    HWND queryEdit_ = nullptr;
    HWND scope_ = nullptr;
    HWND sort_ = nullptr;
    HWND list_ = nullptr;
    HWND status_ = nullptr;
    HWND clearHistory_ = nullptr;
    HWND sortDirection_ = nullptr;
    EngineClient* engine_ = nullptr;
    std::function<void(const ffsearch::Candidate&, const ffsearch::PathReconstruction&)> navigate_;
    std::vector<ffsearch::SearchResult> results_;
    std::vector<ffsearch::Candidate> candidates_;
    std::vector<std::wstring> history_;
    ffsearch::SearchHistory historyStore_;
    std::wstring currentPath_;
    bool visible_ = false;
    bool engineActive_ = false;
    bool retainHistory_ = true;
    bool updatingHistory_ = false;
    bool descending_ = false;
    uint64_t generation_ = 0;

    std::thread worker_;
    std::mutex workMutex_;
    std::condition_variable workCv_;
    std::optional<Work> pendingWork_;
    std::atomic<uint64_t> currentGeneration_{0};
    std::atomic<bool> stopping_{false};
};

} // namespace ffui
