// CandidateWindow.cpp

#include "CandidateWindow.h"
#include "Globals.h"

const wchar_t* CandidateWindow::kClassName = L"MinimalPinyinIME_CandidateWnd";
bool CandidateWindow::s_classRegistered = false;

// Visual layout constants (device pixels; not DPI-aware for simplicity).
static const int kPadX     = 8;   // horizontal padding inside the window
static const int kPadY     = 6;   // vertical padding
static const int kLineH    = 24;  // height of each candidate row
static const int kFontSize = 18;  // font cell height

CandidateWindow::~CandidateWindow()
{
    if (m_hWnd)
    {
        DestroyWindow(m_hWnd);
        m_hWnd = nullptr;
    }
    if (m_hFont)
    {
        DeleteObject(m_hFont);
        m_hFont = nullptr;
    }
}

bool CandidateWindow::RegisterWindowClassOnce()
{
    if (s_classRegistered)
        return true;

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
    wc.lpfnWndProc   = CandidateWindow::WndProc;
    wc.hInstance     = g_hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    wc.lpszClassName = kClassName;

    // RegisterClassEx returns 0 if the class already exists in this process;
    // treat "already registered" (ERROR_CLASS_ALREADY_EXISTS) as success.
    if (RegisterClassExW(&wc) == 0)
    {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return false;
    }
    s_classRegistered = true;
    return true;
}

bool CandidateWindow::EnsureCreated()
{
    if (m_hWnd)
        return true;

    if (!RegisterWindowClassOnce())
        return false;

    // Create a top-most tool-window popup so it does not steal focus and does
    // not appear in the taskbar/alt-tab.
    m_hWnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        kClassName, L"",
        WS_POPUP | WS_BORDER,
        0, 0, 100, 100,
        nullptr, nullptr, g_hInst, nullptr);

    if (!m_hWnd)
        return false;

    // Stash "this" so WndProc can reach the instance.
    SetWindowLongPtrW(m_hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    // Create the CJK-capable font once.
    if (!m_hFont)
    {
        LOGFONTW lf = { 0 };
        lf.lfHeight  = -kFontSize;
        lf.lfWeight  = FW_NORMAL;
        lf.lfCharSet = DEFAULT_CHARSET;
        wcscpy_s(lf.lfFaceName, L"Microsoft JhengHei");
        m_hFont = CreateFontIndirectW(&lf);
    }

    return true;
}

void CandidateWindow::SetCandidates(const std::vector<std::wstring>& candidates)
{
    m_candidates.clear();
    for (size_t i = 0; i < candidates.size() && i < MPIME_MAX_CANDIDATES; ++i)
        m_candidates.push_back(candidates[i]);

    if (m_hWnd && m_visible)
        InvalidateRect(m_hWnd, nullptr, TRUE);
}

// Measures the pixel size needed to display the current candidates as one
// row per candidate.
void CandidateWindow::Measure(int& width, int& height)
{
    height = kPadY * 2 + kLineH * static_cast<int>(m_candidates.empty() ? 1 : m_candidates.size());

    // Estimate a comfortable width by measuring the widest row.
    int maxW = 80;
    HDC hdc = GetDC(m_hWnd);
    HFONT old = static_cast<HFONT>(SelectObject(hdc, m_hFont));
    for (size_t i = 0; i < m_candidates.size(); ++i)
    {
        std::wstring row = std::to_wstring(i + 1) + L"  " + m_candidates[i];
        SIZE sz = { 0 };
        GetTextExtentPoint32W(hdc, row.c_str(), static_cast<int>(row.size()), &sz);
        if (sz.cx > maxW)
            maxW = sz.cx;
    }
    SelectObject(hdc, old);
    ReleaseDC(m_hWnd, hdc);

    width = kPadX * 2 + maxW;
}

void CandidateWindow::Show(POINT pt)
{
    if (!EnsureCreated())
        return;

    if (m_candidates.empty())
    {
        Hide();
        return;
    }

    int w = 0, h = 0;
    Measure(w, h);

    // Keep the window on-screen: clamp to the monitor work area of the point.
    HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    if (GetMonitorInfoW(hMon, &mi))
    {
        if (pt.x + w > mi.rcWork.right)  pt.x = mi.rcWork.right - w;
        if (pt.y + h > mi.rcWork.bottom) pt.y = mi.rcWork.bottom - h;
        if (pt.x < mi.rcWork.left)       pt.x = mi.rcWork.left;
        if (pt.y < mi.rcWork.top)        pt.y = mi.rcWork.top;
    }

    SetWindowPos(m_hWnd, HWND_TOPMOST, pt.x, pt.y, w, h,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    m_visible = true;
    InvalidateRect(m_hWnd, nullptr, TRUE);
    UpdateWindow(m_hWnd);
}

void CandidateWindow::Hide()
{
    if (m_hWnd && m_visible)
    {
        ShowWindow(m_hWnd, SW_HIDE);
        m_visible = false;
    }
}

void CandidateWindow::OnPaint(HWND hWnd)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hWnd, &ps);

    RECT rc;
    GetClientRect(hWnd, &rc);
    FillRect(hdc, &rc, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));

    HFONT old = static_cast<HFONT>(SelectObject(hdc, m_hFont));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(0, 0, 0));

    int y = kPadY;
    for (size_t i = 0; i < m_candidates.size(); ++i)
    {
        // e.g. "1  拼"
        std::wstring text = std::to_wstring(i + 1) + L"  " + m_candidates[i];

        RECT row = { kPadX, y, rc.right - kPadX, y + kLineH };
        DrawTextW(hdc, text.c_str(), -1, &row,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        y += kLineH;
    }

    SelectObject(hdc, old);
    EndPaint(hWnd, &ps);
}

LRESULT CALLBACK CandidateWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    CandidateWindow* self =
        reinterpret_cast<CandidateWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));

    switch (msg)
    {
    case WM_PAINT:
        if (self)
            self->OnPaint(hWnd);
        return 0;

    case WM_ERASEBKGND:
        return 1; // handled in WM_PAINT to avoid flicker

    // Never take activation; input belongs to the edit control.
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_DESTROY:
        return 0;

    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
}
