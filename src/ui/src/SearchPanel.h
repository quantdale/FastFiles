#pragma once

#include <atomic>
#include <condition_variable>
#include <d2d1.h>
#include <dwrite.h>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>
#include <windows.h>
#include <wrl/client.h>

#include "EngineClient.h"
#include "ffsearch/Search.h"
#include "ffsearch/History.h"
#include "Util.h"

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
    void SetDarkTheme(bool dark);
    bool Visible() const { return visible_; }
    void Reposition();
    bool HandleOwnerCommand(WPARAM wParam, LPARAM lParam);
    bool HandleNotify(LPARAM lParam);
    bool HandleDrawItem(LPARAM lParam);
    bool HandleTimer(UINT_PTR timerId);
    bool HandleCompletion(LPARAM lParam);
    void SetEngineActive(bool active);
    void ClearHistory();

    // Task 5: custom-painted query edit and status line. The subclassed controls
    // (SearchEditProc / StatusProc, free functions in the anonymous namespace of
    // SearchPanel.cpp) call back into the panel through these methods.
    void PaintSearchEdit(HWND hwnd);
    void PaintStatus(HWND hwnd);
    // Focus state for the Fluent query edit, set by SearchEditProc on
    // WM_SETFOCUS/WM_KILLFOCUS and read by PaintSearchEdit to draw the accent
    // underline. Public because the subclass proc is a free function.
    bool searchEditFocused_ = false;

private:
    enum class StatusKind { Info, Searching, NoResults };

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
    void UpdateStatus(const std::wstring& text, StatusKind kind = StatusKind::Info);
    void LoadHistory();
    void SaveHistory() const;
    void RecordHistory(const std::wstring& query);
    static std::filesystem::path HistoryPath();

    // Task 5: owner-draw helpers.
    void DrawSearchResultRow(const DRAWITEMSTRUCT* item);
    void DrawComboItem(const DRAWITEMSTRUCT* item);
    void DrawDirectionButton(const DRAWITEMSTRUCT* item);
    int SystemIconIndex(const std::wstring& key);

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
    std::wstring currentPrimaryTerm_;
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

    // Task 5: cached Direct2D/DirectWrite resources for the custom-painted query
    // edit. Created once and reused across paints; torn down and recreated on
    // theme change or DPI change (see PaintSearchEdit).
    Microsoft::WRL::ComPtr<ID2D1Factory> searchFactory_;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> searchTarget_;
    Microsoft::WRL::ComPtr<IDWriteFactory> searchWriteFactory_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> searchFormat_;
    bool searchResourcesReady_ = false;
    float lastSearchDpi_ = -1.0f;

    // Task 5: system image-list (SHIL_SMALL) index per file type key (".txt",
    // "folder"), resolved lazily via SHGetFileInfoW / SHGetStockIconInfo.
    std::map<std::wstring, int> iconIndexCache_;
    StatusKind statusKind_ = StatusKind::Info;
};

} // namespace ffui