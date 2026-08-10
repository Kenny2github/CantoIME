// CandidateWindow.h
//
// A minimal owner-drawn Win32 popup that lists candidate characters as
// "1 拼  2 品  3 聘 ...". It draws itself with GDI using a CJK-capable font.
//
// This window is intentionally dumb: it only displays. The text service owns
// all input handling and decides what to do with number/space/etc. keys.

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include <vector>

class CandidateWindow
{
public:
    CandidateWindow() = default;
    ~CandidateWindow();

    // Creates (once) the underlying popup window. Safe to call repeatedly.
    bool EnsureCreated();

    // Replaces the candidate list to be displayed (max 9 are shown).
    void SetCandidates(const std::vector<std::wstring>& candidates);

    // Shows the window with its top-left corner near screen point pt.
    // (pt is in screen coordinates, typically just below the caret.)
    void Show(POINT pt);

    // Hides the window.
    void Hide();

    bool IsVisible() const { return m_visible; }

private:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void OnPaint(HWND hWnd);
    void Measure(int& width, int& height);
    bool RegisterWindowClassOnce();

    HWND  m_hWnd    = nullptr;
    HFONT m_hFont   = nullptr;
    bool  m_visible = false;
    std::vector<std::wstring> m_candidates;

    static const wchar_t* kClassName;
    static bool s_classRegistered;
};
