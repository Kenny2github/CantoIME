// LangBarButton.cpp

#include "LangBarButton.h"
#include "Globals.h"

#include <olectl.h>   // CONNECT_E_* connection-point HRESULTs
#include <new>

// ---------------------------------------------------------------------------
// Glyph -> HICON
// ---------------------------------------------------------------------------

HICON MakeGlyphIcon(const std::wstring& glyph)
{
    const int cx = GetSystemMetrics(SM_CXSMICON);
    const int cy = GetSystemMetrics(SM_CYSMICON);

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcColor  = CreateCompatibleDC(hdcScreen);
    HDC hdcMask   = CreateCompatibleDC(hdcScreen);

    HBITMAP hbmColor = CreateCompatibleBitmap(hdcScreen, cx, cy);
    HBITMAP hbmMask  = CreateBitmap(cx, cy, 1, 1, nullptr);

    HBITMAP oldColor = static_cast<HBITMAP>(SelectObject(hdcColor, hbmColor));
    HBITMAP oldMask  = static_cast<HBITMAP>(SelectObject(hdcMask,  hbmMask));

    RECT rc = { 0, 0, cx, cy };

    // Color plane: white background, glyph drawn in the system text color.
    FillRect(hdcColor, &rc, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));

    LOGFONTW lf = { 0 };
    lf.lfHeight  = -(cy - 1);
    lf.lfWeight  = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    wcscpy_s(lf.lfFaceName, L"Microsoft JhengHei");
    HFONT font = CreateFontIndirectW(&lf);
    HFONT oldFont = static_cast<HFONT>(SelectObject(hdcColor, font));
    SetBkMode(hdcColor, TRANSPARENT);
    SetTextColor(hdcColor, GetSysColor(COLOR_WINDOWTEXT));
    DrawTextW(hdcColor, glyph.c_str(), -1, &rc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP | DT_NOPREFIX);
    SelectObject(hdcColor, oldFont);
    DeleteObject(font);

    // Mask plane: white (background) pixels become 1 (transparent), the glyph
    // pixels become 0 (opaque). A mono blit keyed on the white bg color does it.
    SetBkColor(hdcColor, RGB(255, 255, 255));
    BitBlt(hdcMask, 0, 0, cx, cy, hdcColor, 0, 0, SRCCOPY);

    SelectObject(hdcColor, oldColor);
    SelectObject(hdcMask,  oldMask);

    ICONINFO ii = { 0 };
    ii.fIcon    = TRUE;
    ii.hbmColor = hbmColor;
    ii.hbmMask  = hbmMask;
    HICON hIcon = CreateIconIndirect(&ii); // copies the bitmaps

    DeleteObject(hbmColor);
    DeleteObject(hbmMask);
    DeleteDC(hdcColor);
    DeleteDC(hdcMask);
    ReleaseDC(nullptr, hdcScreen);

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
    return E_NOTIMPL; // no drop-down menu
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
