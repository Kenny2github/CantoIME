// CandidateWindow.cpp

#include "CandidateWindow.h"
#include "Globals.h"

#include <windowsx.h>   // GET_X_LPARAM / GET_Y_LPARAM
#include <string>

const wchar_t* CandidateWindow::kClassName = L"MinimalPinyinIME_CandidateWnd";
bool CandidateWindow::s_classRegistered = false;

// Visual layout constants (device pixels; not DPI-aware for simplicity).
static const int    kPadX     = 8;   // horizontal padding inside each column
static const int    kPadY     = 6;   // vertical padding
static const int    kLineH    = 24;  // height of each candidate row
static const int    kFontSize = 18;  // font cell height
static const int    kColGap   = 12;  // gap between columns in 2D mode
static const size_t kPageSize = MPIME_MAX_CANDIDATES; // rows per column (9)

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
    if (m_hSelBrush)
    {
        DeleteObject(m_hSelBrush);
        m_hSelBrush = nullptr;
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

    if (!m_hSelBrush)
        m_hSelBrush = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));

    return true;
}

void CandidateWindow::SetCandidates(const std::vector<std::wstring>& candidates)
{
    m_candidates = candidates;
    if (m_visible)
        Relayout();
}

void CandidateWindow::SetView(size_t pageBase, size_t sel, bool twoD, size_t maxCols)
{
    m_pageBase = pageBase;
    m_sel      = sel;
    m_twoD     = twoD;
    m_maxCols  = maxCols < 1 ? 1 : maxCols;
    if (m_visible)
        Relayout();
}

// The row label + candidate text for a global index, e.g. "1  高o".
static std::wstring RowText(size_t row, const std::wstring& cand)
{
    return std::to_wstring(row + 1) + L"  " + cand;
}

// Recomputes the column geometry for the current view and, if visible, resizes
// and repositions the popup at its anchor (clamped to the monitor work area).
void CandidateWindow::Relayout()
{
    m_cols.clear();
    m_totalW = 0;
    m_totalH = 0;

    const size_t n = m_candidates.size();
    if (n == 0 || !m_hWnd)
        return;

    // How many columns to display, starting at m_pageBase.
    size_t cols = 1;
    if (m_twoD)
    {
        size_t remaining = (m_pageBase < n) ? (n - m_pageBase) : 0;
        cols = (remaining + kPageSize - 1) / kPageSize; // ceil
        if (cols > m_maxCols) cols = m_maxCols;
        if (cols < 1) cols = 1;
    }

    HDC hdc = GetDC(m_hWnd);
    HFONT old = static_cast<HFONT>(SelectObject(hdc, m_hFont));

    int x = kPadX;
    size_t maxRows = 0;
    for (size_t c = 0; c < cols; ++c)
    {
        size_t start = m_pageBase + c * kPageSize;
        if (start >= n)
            break;
        size_t rows = n - start;
        if (rows > kPageSize) rows = kPageSize;

        int colW = 60;
        for (size_t r = 0; r < rows; ++r)
        {
            std::wstring row = RowText(r, m_candidates[start + r]);
            SIZE sz = { 0 };
            GetTextExtentPoint32W(hdc, row.c_str(), static_cast<int>(row.size()), &sz);
            if (sz.cx > colW) colW = sz.cx;
        }
        colW += kPadX; // right padding inside the column

        Col col{ x, colW, start, rows };
        m_cols.push_back(col);
        x += colW + kColGap;
        if (rows > maxRows) maxRows = rows;
    }

    SelectObject(hdc, old);
    ReleaseDC(m_hWnd, hdc);

    if (m_cols.empty())
        return;

    // Total width drops the trailing gap and adds the leading pad already in x0.
    m_totalW = m_cols.back().x + m_cols.back().w + kPadX;
    m_totalH = kPadY * 2 + kLineH * static_cast<int>(maxRows);

    if (m_visible)
    {
        POINT pt = m_anchor;
        HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        if (GetMonitorInfoW(hMon, &mi))
        {
            if (pt.x + m_totalW > mi.rcWork.right)  pt.x = mi.rcWork.right - m_totalW;
            if (pt.y + m_totalH > mi.rcWork.bottom) pt.y = mi.rcWork.bottom - m_totalH;
            if (pt.x < mi.rcWork.left)              pt.x = mi.rcWork.left;
            if (pt.y < mi.rcWork.top)               pt.y = mi.rcWork.top;
        }
        SetWindowPos(m_hWnd, HWND_TOPMOST, pt.x, pt.y, m_totalW, m_totalH,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        InvalidateRect(m_hWnd, nullptr, TRUE);
        UpdateWindow(m_hWnd);
    }
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

    m_anchor  = pt;
    m_visible = true;   // set before Relayout so it sizes/positions the window
    Relayout();
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

    for (size_t c = 0; c < m_cols.size(); ++c)
    {
        const Col& col = m_cols[c];

        // Column separator (left edge of every column after the first).
        if (c > 0)
        {
            int sx = col.x - kColGap / 2;
            RECT sep = { sx, kPadY, sx + 1, kPadY + kLineH * static_cast<int>(col.rows) };
            FillRect(hdc, &sep, reinterpret_cast<HBRUSH>(GetStockObject(LTGRAY_BRUSH)));
        }

        for (size_t r = 0; r < col.rows; ++r)
        {
            size_t idx = col.start + r;
            int y = kPadY + static_cast<int>(r) * kLineH;
            RECT row = { col.x, y, col.x + col.w, y + kLineH };

            bool selected = (idx == m_sel);
            if (selected)
            {
                FillRect(hdc, &row, m_hSelBrush);
                SetTextColor(hdc, GetSysColor(COLOR_HIGHLIGHTTEXT));
            }
            else
            {
                SetTextColor(hdc, RGB(0, 0, 0));
            }

            std::wstring text = RowText(r, m_candidates[idx]);
            RECT textRc = { col.x + 2, y, col.x + col.w, y + kLineH };
            DrawTextW(hdc, text.c_str(), -1, &textRc,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }

    SelectObject(hdc, old);
    EndPaint(hWnd, &ps);
}

void CandidateWindow::OnClick(int x, int y)
{
    for (const Col& col : m_cols)
    {
        if (x < col.x || x >= col.x + col.w)
            continue;
        if (y < kPadY)
            return;
        size_t r = static_cast<size_t>((y - kPadY) / kLineH);
        if (r >= col.rows)
            return;
        size_t idx = col.start + r;
        if (m_onAccept)
            m_onAccept(idx);
        return;
    }
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

    case WM_LBUTTONDOWN:
        if (self)
            self->OnClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;

    // Never take activation; input belongs to the edit control.
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_DESTROY:
        return 0;

    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
}
