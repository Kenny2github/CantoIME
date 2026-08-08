// LangBarButton.h
//
// A minimal TSF language-bar item button. CantoIME shows two of them:
//   * the main input-method button (the input-indicator glyph, 粵), and
//   * the input-mode button (中 / 英) that toggles Chinese/English input.
//
// Each button implements ITfLangBarItemButton (so the language bar can query
// its icon/text and deliver clicks) plus ITfSource (so the language bar can
// advise a sink we notify when our icon/text changes). The glyph is supplied by
// a callback and rendered to an HICON on demand, so the mode button can flip
// between 中 and 英 just by returning a different string.

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <msctf.h>
#include <string>
#include <functional>

class CLangBarButton final : public ITfLangBarItemButton, public ITfSource
{
public:
    // Returns the glyph currently shown on the button (e.g. L"粵" or L"中").
    using GlyphFn = std::function<std::wstring()>;
    // Invoked on a left click.
    using ClickFn = std::function<void()>;

    CLangBarButton(REFCLSID clsidService, REFGUID guidItem, DWORD style,
                   ULONG sort, const std::wstring& description);

    void SetGlyph(GlyphFn fn) { m_glyph = std::move(fn); }
    void SetOnClick(ClickFn fn) { m_onClick = std::move(fn); }

    // Tells the language bar to re-query our icon/text (after a mode change).
    void Update();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfLangBarItem
    STDMETHODIMP GetInfo(TF_LANGBARITEMINFO* pInfo) override;
    STDMETHODIMP GetStatus(DWORD* pdwStatus) override;
    STDMETHODIMP Show(BOOL fShow) override;
    STDMETHODIMP GetTooltipString(BSTR* pbstrToolTip) override;

    // ITfLangBarItemButton
    STDMETHODIMP OnClick(TfLBIClick click, POINT pt, const RECT* prcArea) override;
    STDMETHODIMP InitMenu(ITfMenu* pMenu) override;
    STDMETHODIMP OnMenuSelect(UINT wID) override;
    STDMETHODIMP GetIcon(HICON* phIcon) override;
    STDMETHODIMP GetText(BSTR* pbstrText) override;

    // ITfSource
    STDMETHODIMP AdviseSink(REFIID riid, IUnknown* punk, DWORD* pdwCookie) override;
    STDMETHODIMP UnadviseSink(DWORD dwCookie) override;

private:
    ~CLangBarButton();

    std::wstring CurrentGlyph() const;

    LONG                m_cRef = 1;
    TF_LANGBARITEMINFO  m_info = {};
    std::wstring        m_tooltip;
    GlyphFn             m_glyph;
    ClickFn             m_onClick;
    ITfLangBarItemSink* m_pSink = nullptr; // advised by the language bar
};

// Renders a short glyph string to a small HICON using a CJK font, with a
// transparent background. The caller owns the returned icon.
HICON MakeGlyphIcon(const std::wstring& glyph);
