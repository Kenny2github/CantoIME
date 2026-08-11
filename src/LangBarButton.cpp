// LangBarButton.cpp

#include "LangBarButton.h"
#include "Globals.h"

#include <olectl.h>   // CONNECT_E_* connection-point HRESULTs
#include <new>

// ---------------------------------------------------------------------------
// Glyph -> HICON
// ---------------------------------------------------------------------------

// True when the taskbar/system UI is in dark mode, so glyphs should be light.
static bool SystemUsesDarkTheme()
{
    HKEY hKey = nullptr;
    DWORD light = 1, cb = sizeof(light), type = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        RegQueryValueExW(hKey, L"SystemUsesLightTheme", nullptr, &type,
                         reinterpret_cast<LPBYTE>(&light), &cb);
        RegCloseKey(hKey);
    }
    return (type == REG_DWORD && light == 0);
}

HICON MakeGlyphIcon(const std::wstring& glyph)
{
    const int cx = GetSystemMetrics(SM_CXSMICON);
    const int cy = GetSystemMetrics(SM_CYSMICON);

    // Glyph colour follows the theme: near-black on light, near-white on dark.
    const bool dark = SystemUsesDarkTheme();
    const int  tone = dark ? 240 : 16;

    // Target ink HEIGHT at the final size: ~14px in a 16px cell, scaled with the
    // cell so it holds at higher DPI.
    const int targetH = cy * 14 / 16;

    // Supersample: render large, then box-downscale for smooth anti-aliasing.
    // Rendering straight at ~12px gave jagged edges; a 4x render + HALFTONE
    // shrink matches Quick's smoothness.
    const int SS = 4;
    const int bw = cx * SS, bh = cy * SS;

    auto makeDib = [](HDC dc, int w, int h, void** bitsOut) -> HBITMAP
    {
        BITMAPINFO bmi = { 0 };
        bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth       = w;
        bmi.bmiHeader.biHeight      = -h; // top-down
        bmi.bmiHeader.biPlanes      = 1;
        bmi.bmiHeader.biBitCount    = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        return CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, bitsOut, nullptr, 0);
    };

    HDC hdcBig = CreateCompatibleDC(nullptr);
    void* bigBits = nullptr;
    HBITMAP hBig = makeDib(hdcBig, bw, bh, &bigBits);
    if (!hBig) { DeleteDC(hdcBig); return nullptr; }
    HBITMAP oldBig = static_cast<HBITMAP>(SelectObject(hdcBig, hBig));

    // Draws the glyph white-on-black into the big DIB at the given em height and
    // returns the ink's pixel height (0 if none).
    auto drawBig = [&](int emPx) -> int
    {
        memset(bigBits, 0, static_cast<size_t>(bw) * bh * 4);
        LOGFONTW lf = { 0 };
        lf.lfHeight  = -emPx;
        lf.lfWeight  = FW_NORMAL;
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfQuality = ANTIALIASED_QUALITY;
        wcscpy_s(lf.lfFaceName, L"Microsoft JhengHei");
        HFONT f = CreateFontIndirectW(&lf);
        HFONT of = static_cast<HFONT>(SelectObject(hdcBig, f));
        RECT rc = { 0, 0, bw, bh };
        SetBkMode(hdcBig, TRANSPARENT);
        SetTextColor(hdcBig, RGB(255, 255, 255));
        DrawTextW(hdcBig, glyph.c_str(), -1, &rc,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP | DT_NOPREFIX);
        SelectObject(hdcBig, of);
        DeleteObject(f);
        GdiFlush();

        int minY = bh, maxY = -1;
        const BYTE* q = static_cast<const BYTE*>(bigBits);
        for (int y = 0; y < bh; ++y)
            for (int x = 0; x < bw; ++x)
                if (q[(y * bw + x) * 4 + 2] > 24) // coverage threshold
                {
                    if (y < minY) minY = y;
                    if (y > maxY) maxY = y;
                    break; // this row has ink; next row
                }
        return (maxY < 0) ? 0 : (maxY - minY + 1);
    };

    // Pass 1: measure at em = big-cell height. Pass 2: scale so the ink height is
    // targetH (in final px) -> targetH*SS in the big DIB. Then re-render.
    int inkH = drawBig(bh);
    if (inkH > 0)
    {
        int em = static_cast<int>(static_cast<double>(bh) * (targetH * SS) / inkH + 0.5);
        if (em < SS)      em = SS;
        if (em > 3 * bh)  em = 3 * bh;
        drawBig(em);
    }

    // Box-downscale the big DIB into the final cx*cy DIB (smooth AA).
    HDC hdcSmall = CreateCompatibleDC(nullptr);
    void* bits = nullptr;
    HBITMAP hDib = makeDib(hdcSmall, cx, cy, &bits);
    if (!hDib) { SelectObject(hdcBig, oldBig); DeleteObject(hBig); DeleteDC(hdcBig); DeleteDC(hdcSmall); return nullptr; }
    HBITMAP oldSmall = static_cast<HBITMAP>(SelectObject(hdcSmall, hDib));
    SetStretchBltMode(hdcSmall, HALFTONE);
    SetBrushOrgEx(hdcSmall, 0, 0, nullptr);
    StretchBlt(hdcSmall, 0, 0, cx, cy, hdcBig, 0, 0, bw, bh, SRCCOPY);
    GdiFlush();

    // Convert coverage -> premultiplied BGRA in the theme tone.
    BYTE* px = static_cast<BYTE*>(bits);
    for (int i = 0; i < cx * cy; ++i, px += 4)
    {
        const int cov = px[2]; // R == G == B for grayscale AA
        px[0] = static_cast<BYTE>(tone * cov / 255); // B (premultiplied)
        px[1] = static_cast<BYTE>(tone * cov / 255); // G
        px[2] = static_cast<BYTE>(tone * cov / 255); // R
        px[3] = static_cast<BYTE>(cov);              // A
    }

    SelectObject(hdcSmall, oldSmall);
    SelectObject(hdcBig, oldBig);
    DeleteObject(hBig);
    DeleteDC(hdcBig);

    HBITMAP hMask = CreateBitmap(cx, cy, 1, 1, nullptr); // all-zero AND mask
    ICONINFO ii = { 0 };
    ii.fIcon    = TRUE;
    ii.hbmColor = hDib;
    ii.hbmMask  = hMask;
    HICON hIcon = CreateIconIndirect(&ii); // copies the bitmaps

    DeleteObject(hMask);
    DeleteObject(hDib);
    DeleteDC(hdcSmall);
    return hIcon;
}

// ---------------------------------------------------------------------------
// CLangBarButton
// ---------------------------------------------------------------------------

CLangBarButton::CLangBarButton(REFCLSID clsidService, REFGUID guidItem, DWORD style,
                               ULONG sort, const std::wstring& description)
{
    m_info.clsidService = clsidService;
    m_info.guidItem     = guidItem;
    m_info.dwStyle      = style;
    m_info.ulSort       = sort;
    // szDescription is a fixed-size array; copy safely.
    wcsncpy_s(m_info.szDescription, description.c_str(), _TRUNCATE);
    m_tooltip = description;
    DllAddRef();
}

CLangBarButton::~CLangBarButton()
{
    if (m_pSink)
    {
        m_pSink->Release();
        m_pSink = nullptr;
    }
    DllRelease();
}

std::wstring CLangBarButton::CurrentGlyph() const
{
    return m_glyph ? m_glyph() : std::wstring();
}

void CLangBarButton::Update()
{
    if (m_pSink)
        m_pSink->OnUpdate(TF_LBI_ICON | TF_LBI_TEXT | TF_LBI_STATUS);
}

// ---- IUnknown ----

STDMETHODIMP CLangBarButton::QueryInterface(REFIID riid, void** ppv)
{
    if (ppv == nullptr)
        return E_INVALIDARG;
    *ppv = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, IID_ITfLangBarItem) ||
        IsEqualIID(riid, IID_ITfLangBarItemButton))
    {
        *ppv = static_cast<ITfLangBarItemButton*>(this);
    }
    else if (IsEqualIID(riid, IID_ITfSource))
    {
        *ppv = static_cast<ITfSource*>(this);
    }
    if (*ppv)
    {
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CLangBarButton::AddRef()
{
    return InterlockedIncrement(&m_cRef);
}

STDMETHODIMP_(ULONG) CLangBarButton::Release()
{
    ULONG cr = InterlockedDecrement(&m_cRef);
    if (cr == 0)
        delete this;
    return cr;
}

// ---- ITfLangBarItem ----

STDMETHODIMP CLangBarButton::GetInfo(TF_LANGBARITEMINFO* pInfo)
{
    if (pInfo == nullptr)
        return E_INVALIDARG;
    *pInfo = m_info;
    return S_OK;
}

STDMETHODIMP CLangBarButton::GetStatus(DWORD* pdwStatus)
{
    if (pdwStatus == nullptr)
        return E_INVALIDARG;
    *pdwStatus = 0; // visible and enabled
    return S_OK;
}

STDMETHODIMP CLangBarButton::Show(BOOL /*fShow*/)
{
    return E_NOTIMPL; // the language bar controls visibility for buttons
}

STDMETHODIMP CLangBarButton::GetTooltipString(BSTR* pbstrToolTip)
{
    if (pbstrToolTip == nullptr)
        return E_INVALIDARG;
    std::wstring tip = m_tooltipFn ? m_tooltipFn() : m_tooltip;
    *pbstrToolTip = SysAllocString(tip.c_str());
    return (*pbstrToolTip != nullptr) ? S_OK : E_OUTOFMEMORY;
}

// ---- ITfLangBarItemButton ----

STDMETHODIMP CLangBarButton::OnClick(TfLBIClick click, POINT /*pt*/, const RECT* /*prcArea*/)
{
    if (click == TF_LBI_CLK_LEFT && m_onClick)
        m_onClick();
    return S_OK;
}

STDMETHODIMP CLangBarButton::InitMenu(ITfMenu* /*pMenu*/)
{
    return E_NOTIMPL; // no context menu (needs the Language Bar; unused)
}

STDMETHODIMP CLangBarButton::OnMenuSelect(UINT /*wID*/)
{
    return E_NOTIMPL;
}

STDMETHODIMP CLangBarButton::GetIcon(HICON* phIcon)
{
    if (phIcon == nullptr)
        return E_INVALIDARG;
    *phIcon = MakeGlyphIcon(CurrentGlyph()); // language bar takes ownership
    return (*phIcon != nullptr) ? S_OK : E_FAIL;
}

STDMETHODIMP CLangBarButton::GetText(BSTR* pbstrText)
{
    if (pbstrText == nullptr)
        return E_INVALIDARG;
    *pbstrText = SysAllocString(CurrentGlyph().c_str());
    return (*pbstrText != nullptr) ? S_OK : E_OUTOFMEMORY;
}

// ---- ITfSource ----

STDMETHODIMP CLangBarButton::AdviseSink(REFIID riid, IUnknown* punk, DWORD* pdwCookie)
{
    if (punk == nullptr || pdwCookie == nullptr)
        return E_INVALIDARG;
    if (!IsEqualIID(riid, IID_ITfLangBarItemSink))
        return CONNECT_E_CANNOTCONNECT;
    if (m_pSink != nullptr)
        return CONNECT_E_ADVISELIMIT; // one sink is enough

    if (FAILED(punk->QueryInterface(IID_ITfLangBarItemSink,
                                    reinterpret_cast<void**>(&m_pSink))))
        return E_NOINTERFACE;

    *pdwCookie = 0; // single sink -> fixed cookie
    return S_OK;
}

STDMETHODIMP CLangBarButton::UnadviseSink(DWORD /*dwCookie*/)
{
    if (m_pSink == nullptr)
        return CONNECT_E_NOCONNECTION;
    m_pSink->Release();
    m_pSink = nullptr;
    return S_OK;
}
