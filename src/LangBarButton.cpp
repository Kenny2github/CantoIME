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

    // 32-bit top-down DIB so we own the alpha channel and get clean, halo-free
    // anti-aliasing (the old 1-bit mask left grey edge pixels as a white fringe).
    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = cx;
    bmi.bmiHeader.biHeight      = -cy; // top-down
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC hdcMem = CreateCompatibleDC(nullptr);
    HBITMAP hDib = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hDib) { DeleteDC(hdcMem); return nullptr; }
    memset(bits, 0, static_cast<size_t>(cx) * cy * 4);

    HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(hdcMem, hDib));

    LOGFONTW lf = { 0 };
    lf.lfHeight  = -(cy - 1);
    lf.lfWeight  = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = ANTIALIASED_QUALITY; // grayscale AA (no ClearType colour fringe)
    wcscpy_s(lf.lfFaceName, L"Microsoft JhengHei");
    HFONT font = CreateFontIndirectW(&lf);
    HFONT oldFont = static_cast<HFONT>(SelectObject(hdcMem, font));

    // Draw the glyph white on the zeroed (black) DIB; the white intensity is the
    // per-pixel coverage. GDI leaves the alpha byte at 0, so we rebuild it below.
    RECT rc = { 0, 0, cx, cy };
    SetBkMode(hdcMem, TRANSPARENT);
    SetTextColor(hdcMem, RGB(255, 255, 255));
    DrawTextW(hdcMem, glyph.c_str(), -1, &rc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP | DT_NOPREFIX);

    SelectObject(hdcMem, oldFont);
    DeleteObject(font);
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

    SelectObject(hdcMem, oldBmp);

    HBITMAP hMask = CreateBitmap(cx, cy, 1, 1, nullptr); // all-zero AND mask
    ICONINFO ii = { 0 };
    ii.fIcon    = TRUE;
    ii.hbmColor = hDib;
    ii.hbmMask  = hMask;
    HICON hIcon = CreateIconIndirect(&ii); // copies the bitmaps

    DeleteObject(hMask);
    DeleteObject(hDib);
    DeleteDC(hdcMem);
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

STDMETHODIMP CLangBarButton::InitMenu(ITfMenu* pMenu)
{
    if (!m_menu || pMenu == nullptr)
        return E_NOTIMPL;
    m_menu(pMenu);
    return S_OK;
}

STDMETHODIMP CLangBarButton::OnMenuSelect(UINT wID)
{
    if (m_onMenuSelect)
        m_onMenuSelect(wID);
    return S_OK;
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
