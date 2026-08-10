// CandidateWindow.h
//
// An owner-drawn Win32 popup that lists candidate characters. It supports a
// selected candidate (highlighted), single-column (vertical) layout, and a
// two-dimensional grid of 9-candidate pages laid out in columns.
//
// The window is a "dumb" view: the text service owns the candidate list and all
// navigation state (which candidate is selected, which page is at the top-left,
// whether 2D paging is engaged) and pushes that state in via SetView(). The
// window only measures, paints, and hit-tests. When a candidate is clicked it
// calls back through the accept callback with the *global* candidate index.

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include <vector>
#include <functional>

class CandidateWindow
{
public:
    CandidateWindow() = default;
    ~CandidateWindow();

    // Invoked when the user clicks a candidate. The argument is the global
    // index into the candidate list last passed to SetCandidates().
    using AcceptCb = std::function<void(size_t globalIndex)>;

    // Creates (once) the underlying popup window. Safe to call repeatedly.
    bool EnsureCreated();

    // Replaces the full candidate list to be displayed.
    void SetCandidates(const std::vector<std::wstring>& candidates);

    // Pushes the current view state: index of the top-left cell (a multiple of
    // the page size), the globally-selected index, whether 2D paging is engaged,
    // and the maximum number of columns to show in 2D mode.
    void SetView(size_t pageBase, size_t sel, bool twoD, size_t maxCols);

    void SetOnAccept(AcceptCb cb) { m_onAccept = std::move(cb); }

    // Shows the window with its top-left corner near screen point pt.
    void Show(POINT pt);

    // Hides the window.
    void Hide();

    bool IsVisible() const { return m_visible; }

    // True when the window is visible and the screen point lies inside it.
    // Lets the wheel hook capture ticks only while the cursor is over the
    // list, passing scroll through to the app underneath otherwise.
    bool HitTestScreen(POINT pt) const;

    // Number of 2D columns that actually fit on screen starting at pageBase,
    // given the desired mode and column cap. The text service queries this so
    // navigation never selects a column the window had to drop for width.
    size_t ColumnsThatFit(size_t pageBase, bool twoD, size_t maxCols) const;

private:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void OnPaint(HWND hWnd);
    void OnClick(int x, int y);
    void Relayout();                 // recompute geometry, resize/move at anchor
    bool RegisterWindowClassOnce();

    // Builds the visible column layout from pageBase, capped both by maxCols and
    // by the anchor monitor's work-area width (so the popup never runs off
    // screen). At least one column is always returned.
    std::vector<Col> BuildCols(size_t pageBase, bool twoD, size_t maxCols) const;
    // Work-area width (px) of the monitor nearest the current anchor.
    int AvailWidth() const;

    // One displayed column: its left edge, width, first global candidate index,
    // and how many rows it holds (up to the page size).
    struct Col { int x; int w; size_t start; size_t rows; };

    HWND  m_hWnd    = nullptr;
    HFONT m_hFont   = nullptr;
    HBRUSH m_hSelBrush = nullptr;
    bool  m_visible = false;

    std::vector<std::wstring> m_candidates; // full display list
    size_t m_pageBase = 0;                  // global index of top-left cell
    size_t m_sel      = 0;                  // globally selected index
    bool   m_twoD     = false;
    size_t m_maxCols  = 5;

    POINT m_anchor = { 0, 0 };              // where Show() last placed us
    std::vector<Col> m_cols;                // layout cache (from Relayout)
    int m_totalW = 0;
    int m_totalH = 0;

    AcceptCb m_onAccept;

    static const wchar_t* kClassName;
    static bool s_classRegistered;
};
