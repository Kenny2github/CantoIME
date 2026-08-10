// TextService.cpp
//
// Implementation of the core TSF text service.
//
// KEY HANDLING SUMMARY (documented behavior):
//   While NOT composing:
//     - a-z         : start a composition, begin the romanization buffer, eat key.
//     - anything    : pass through (not eaten).
//   While composing:
//     - a-z         : append to buffer, refresh candidates, eat key.
//     - Backspace   : remove last letter; if buffer becomes empty, cancel
//                     composition. Eat key. (If not composing, pass through.)
//     - 1..9        : if a candidate exists at that index, commit it, eat key;
//                     otherwise the digit is treated as a normal keystroke and
//                     appended is NOT done (digits are never part of romanization),
//                     so we commit the raw buffer and let the digit pass.
//     - Space       : commit first candidate; if none, commit raw buffer.
//     - Enter       : commit the raw typed romanization text as-is.
//     - Esc         : cancel composition, clear buffer, eat key.
//     - Any other    : SIMPLEST COHERENT CHOICE -> commit the current best
//                     result (first candidate if any, else raw buffer), then
//                     let the key pass through to the app (not eaten). This
//                     matches how most IMEs behave when you type punctuation
//                     mid-composition.

#include "TextService.h"
#include "EditSession.h"
#include "LangBarButton.h"
#include "OptionsDialog.h"
#include "Settings.h"

#include <msctf.h>
#include <ctffunc.h>   // ITfContextView / display helpers live in msctf; kept for completeness
#include <windowsx.h>  // GET_WHEEL_DELTA_WPARAM
#include <unordered_set>
#include <functional>
#include <algorithm>

// Statics backing the low-level mouse wheel hook.
HHOOK         CTextService::s_wheelHook  = nullptr;
CTextService* CTextService::s_wheelOwner = nullptr;

// Statics backing the low-level keyboard hook (lone-Shift mode toggle).
HHOOK         CTextService::s_kbdHook  = nullptr;
CTextService* CTextService::s_kbdOwner = nullptr;

// Posted from the keyboard hook to the message-only window to run the toggle on
// the UI thread.
static const UINT WM_CANTO_TOGGLE = WM_APP + 0x101;
static const wchar_t* kMsgWndClass = L"CantoIME_" CANTO_VARIANT_TAG L"_MsgWnd";

static bool IsShiftVK(WPARAM vk); // defined with the key-handling helpers below

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

CTextService::CTextService()
    : m_cRef(1),
      m_pThreadMgr(nullptr),
      m_tid(TF_CLIENTID_NULL),
      m_pComposition(nullptr),
      m_dwThreadMgrEventSinkCookie(TF_INVALID_COOKIE)
{
    DllAddRef();
}

CTextService::~CTextService()
{
    DllRelease();
}

HRESULT CreateTextServiceInstance(REFIID riid, void** ppv)
{
    if (ppv == nullptr)
        return E_INVALIDARG;
    *ppv = nullptr;

    CTextService* p = new (std::nothrow) CTextService();
    if (p == nullptr)
        return E_OUTOFMEMORY;

    HRESULT hr = p->QueryInterface(riid, ppv);
    p->Release(); // balance the constructor's implicit ref
    return hr;
}

// ---------------------------------------------------------------------------
// IUnknown
// ---------------------------------------------------------------------------

STDMETHODIMP CTextService::QueryInterface(REFIID riid, void** ppv)
{
    if (ppv == nullptr)
        return E_INVALIDARG;
    *ppv = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, IID_ITfTextInputProcessor))
    {
        *ppv = static_cast<ITfTextInputProcessor*>(this);
    }
    else if (IsEqualIID(riid, IID_ITfTextInputProcessorEx))
    {
        *ppv = static_cast<ITfTextInputProcessorEx*>(this);
    }
    else if (IsEqualIID(riid, IID_ITfThreadMgrEventSink))
    {
        *ppv = static_cast<ITfThreadMgrEventSink*>(this);
    }
    else if (IsEqualIID(riid, IID_ITfKeyEventSink))
    {
        *ppv = static_cast<ITfKeyEventSink*>(this);
    }
    else if (IsEqualIID(riid, IID_ITfCompositionSink))
    {
        *ppv = static_cast<ITfCompositionSink*>(this);
    }
    else if (IsEqualIID(riid, IID_ITfFunctionProvider))
    {
        *ppv = static_cast<ITfFunctionProvider*>(this);
    }
    else if (IsEqualIID(riid, IID_ITfFunction))
    {
        *ppv = static_cast<ITfFunction*>(static_cast<ITfFnConfigure*>(this));
    }
    else if (IsEqualIID(riid, IID_ITfFnConfigure))
    {
        *ppv = static_cast<ITfFnConfigure*>(this);
    }

    if (*ppv)
    {
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CTextService::AddRef()
{
    return InterlockedIncrement(&m_cRef);
}

STDMETHODIMP_(ULONG) CTextService::Release()
{
    ULONG cr = InterlockedDecrement(&m_cRef);
    if (cr == 0)
        delete this;
    return cr;
}

// ---------------------------------------------------------------------------
// Activation lifecycle
// ---------------------------------------------------------------------------

STDMETHODIMP CTextService::Activate(ITfThreadMgr* ptim, TfClientId tid)
{
    return ActivateEx(ptim, tid, 0);
}

STDMETHODIMP CTextService::ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD /*dwFlags*/)
{
    m_pThreadMgr = ptim;
    if (m_pThreadMgr)
        m_pThreadMgr->AddRef();
    m_tid = tid;

    if (!InitThreadMgrEventSink())
        goto fail;
    if (!InitKeyEventSink())
        goto fail;

    // Prepare (but do not show) the candidate window, and route candidate
    // clicks back to us so a mouse click accepts that candidate.
    m_candWnd.EnsureCreated();
    m_candWnd.SetOnAccept([this](size_t globalIndex)
    {
        OnCandidateClicked(globalIndex);
    });

    // Apply per-user settings: candidate font size + load the selected dictionary.
    ApplySettings();

    // Add the language-bar buttons (main 粵 icon + 中/英 input-mode toggle).
    AddLangBarButtons();

    // Watch for a lone-Shift press to toggle input mode (see the header note).
    CreateMsgWindow();
    InstallKeyboardHook();

    return S_OK;

fail:
    Deactivate();
    return E_FAIL;
}

STDMETHODIMP CTextService::Deactivate()
{
    // Ensure any in-flight composition is torn down and UI hidden.
    RemoveKeyboardHook();
    DestroyMsgWindow();
    RemoveLangBarButtons();
    RemoveWheelHook();
    ClearContext();
    m_candWnd.Hide();
    m_romanization.clear();
    m_candidates.clear();
    ResetSelection();
    m_twoD = false;

    if (m_pComposition)
    {
        m_pComposition->Release();
        m_pComposition = nullptr;
    }

    UninitKeyEventSink();
    UninitThreadMgrEventSink();

    if (m_pThreadMgr)
    {
        m_pThreadMgr->Release();
        m_pThreadMgr = nullptr;
    }
    m_tid = TF_CLIENTID_NULL;

    return S_OK;
}

// ---------------------------------------------------------------------------
// Language-bar items + input mode
// ---------------------------------------------------------------------------

void CTextService::AddLangBarButtons()
{
    if (!m_pThreadMgr || m_pMainButton || m_pModeButton)
        return;

    ITfLangBarItemMgr* pMgr = nullptr;
    if (FAILED(m_pThreadMgr->QueryInterface(IID_ITfLangBarItemMgr,
                                            reinterpret_cast<void**>(&pMgr))))
        return;

    // Main input-method button: the input-indicator glyph (粵). Static.
    m_pMainButton = new (std::nothrow) CLangBarButton(
        CLSID_CantoIME, GUID_CantoMainButton,
        TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_SHOWNINTRAY, 0, L"CantoIME");
    if (m_pMainButton)
    {
        // U+7CB5 粵 (Yue / Cantonese). Use an escape, not a literal, so the
        // glyph does not depend on the source-file encoding MSVC assumes.
        m_pMainButton->SetGlyph([] { return std::wstring(L"\u7CB5"); }); // 粵
        pMgr->AddItem(m_pMainButton);
    }

    // Input-mode button: 中 / 英, click toggles. GUID_LBI_INPUTMODE marks it as
    // the standard conversion-mode indicator.
    m_pModeButton = new (std::nothrow) CLangBarButton(
        CLSID_CantoIME, GUID_LBI_INPUTMODE,
        TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_SHOWNINTRAY, 1, L"Input mode");
    if (m_pModeButton)
    {
        // 中 = 中 (Chinese), 英 = 英 (English).
        m_pModeButton->SetGlyph([this] {
            return std::wstring(m_chineseMode ? L"\u4E2D" : L"\u82F1"); // 中 / 英
        });
        m_pModeButton->SetTooltip([this] {
            return std::wstring(m_chineseMode ? L"Chinese mode" : L"English mode");
        });
        m_pModeButton->SetOnClick([this] { ToggleInputMode(); });
        pMgr->AddItem(m_pModeButton);
    }

    pMgr->Release();
}

void CTextService::RemoveLangBarButtons()
{
    ITfLangBarItemMgr* pMgr = nullptr;
    if (m_pThreadMgr &&
        SUCCEEDED(m_pThreadMgr->QueryInterface(IID_ITfLangBarItemMgr,
                                               reinterpret_cast<void**>(&pMgr))))
    {
        if (m_pMainButton) pMgr->RemoveItem(m_pMainButton);
        if (m_pModeButton) pMgr->RemoveItem(m_pModeButton);
        pMgr->Release();
    }

    if (m_pMainButton) { m_pMainButton->Release(); m_pMainButton = nullptr; }
    if (m_pModeButton) { m_pModeButton->Release(); m_pModeButton = nullptr; }
}

// Flips between Chinese and English input. Any in-flight composition is
// committed first (as pressing Shift does in Bopomofo), then the input-mode
// button refreshes its 中/英 glyph.
void CTextService::ToggleInputMode()
{
    if (IsComposing() && m_pContext)
    {
        if (!m_candidates.empty())
            CommitCandidate(m_pContext, m_sel);
        else
            CommitRaw(m_pContext);
    }

    m_chineseMode = !m_chineseMode;

    if (m_pModeButton)
        m_pModeButton->Update();
}

// ---- Lone-Shift toggle: message-only window + low-level keyboard hook --------

LRESULT CALLBACK CTextService::MsgWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    CTextService* self = reinterpret_cast<CTextService*>(
        GetWindowLongPtrW(hWnd, GWLP_USERDATA));

    switch (msg)
    {
    case WM_CANTO_TOGGLE:
        if (self)
            self->ToggleInputMode();
        return 0;

    // Light/dark theme switched: WM_SETTINGCHANGE carries "ImmersiveColorSet".
    // Re-query the button icons so their glyphs recolor with the OS, the way
    // the built-in IMEs do.
    case WM_SETTINGCHANGE:
        if (self && lParam &&
            lstrcmpiW(reinterpret_cast<LPCWSTR>(lParam), L"ImmersiveColorSet") == 0)
            self->RefreshLangBarIcons();
        break;

    case WM_THEMECHANGED:
        if (self)
            self->RefreshLangBarIcons();
        break;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void CTextService::RefreshLangBarIcons()
{
    if (m_pMainButton) m_pMainButton->Update();
    if (m_pModeButton) m_pModeButton->Update();
}

void CTextService::CreateMsgWindow()
{
    if (m_hMsgWnd)
        return;

    static bool s_registered = false;
    if (!s_registered)
    {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = CTextService::MsgWndProc;
        wc.hInstance     = g_hInst;
        wc.lpszClassName = kMsgWndClass;
        if (RegisterClassExW(&wc) == 0 &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return;
        s_registered = true;
    }

    // A hidden *top-level* tool window (not a message-only window): only
    // top-level windows receive the WM_SETTINGCHANGE theme broadcast. It is
    // never shown and is kept out of the taskbar/alt-tab.
    m_hMsgWnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                kMsgWndClass, L"", WS_POPUP,
                                0, 0, 0, 0, nullptr, nullptr, g_hInst, nullptr);
    if (m_hMsgWnd)
        SetWindowLongPtrW(m_hMsgWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
}

void CTextService::DestroyMsgWindow()
{
    if (m_hMsgWnd)
    {
        DestroyWindow(m_hMsgWnd);
        m_hMsgWnd = nullptr;
    }
}

void CTextService::InstallKeyboardHook()
{
    s_kbdOwner = this;
    if (s_kbdHook)
        return;
    s_kbdHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, g_hInst, 0);
}

void CTextService::RemoveKeyboardHook()
{
    if (s_kbdHook)
    {
        UnhookWindowsHookEx(s_kbdHook);
        s_kbdHook = nullptr;
    }
    if (s_kbdOwner == this)
        s_kbdOwner = nullptr;
    m_kbShiftDown = false;
    m_kbShiftLone = false;
}

LRESULT CALLBACK CTextService::LowLevelKeyboardProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && s_kbdOwner)
    {
        const KBDLLHOOKSTRUCT* p = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        const bool isShift = IsShiftVK(p->vkCode);

        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)
        {
            if (isShift)
            {
                // First press (ignore auto-repeat): arm the lone-Shift toggle.
                if (!s_kbdOwner->m_kbShiftDown)
                {
                    s_kbdOwner->m_kbShiftDown = true;
                    s_kbdOwner->m_kbShiftLone = true;
                }
            }
            else
            {
                s_kbdOwner->m_kbShiftLone = false; // another key -> not a lone tap
            }
        }
        else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP)
        {
            if (isShift)
            {
                const bool lone = s_kbdOwner->m_kbShiftLone;
                s_kbdOwner->m_kbShiftDown = false;
                s_kbdOwner->m_kbShiftLone = false;
                // Run the toggle on the UI thread, not inside this hook.
                if (lone && s_kbdOwner->m_hMsgWnd)
                    PostMessageW(s_kbdOwner->m_hMsgWnd, WM_CANTO_TOGGLE, 0, 0);
            }
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Sink advise / unadvise
// ---------------------------------------------------------------------------

bool CTextService::InitThreadMgrEventSink()
{
    ITfSource* pSource = nullptr;
    if (FAILED(m_pThreadMgr->QueryInterface(IID_ITfSource, reinterpret_cast<void**>(&pSource))))
        return false;

    bool ok = SUCCEEDED(pSource->AdviseSink(
        IID_ITfThreadMgrEventSink,
        static_cast<ITfThreadMgrEventSink*>(this),
        &m_dwThreadMgrEventSinkCookie));

    pSource->Release();
    return ok;
}

void CTextService::UninitThreadMgrEventSink()
{
    if (m_dwThreadMgrEventSinkCookie == TF_INVALID_COOKIE || !m_pThreadMgr)
        return;

    ITfSource* pSource = nullptr;
    if (SUCCEEDED(m_pThreadMgr->QueryInterface(IID_ITfSource, reinterpret_cast<void**>(&pSource))))
    {
        pSource->UnadviseSink(m_dwThreadMgrEventSinkCookie);
        pSource->Release();
    }
    m_dwThreadMgrEventSinkCookie = TF_INVALID_COOKIE;
}

bool CTextService::InitKeyEventSink()
{
    ITfKeystrokeMgr* pKeyMgr = nullptr;
    if (FAILED(m_pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, reinterpret_cast<void**>(&pKeyMgr))))
        return false;

    bool ok = SUCCEEDED(pKeyMgr->AdviseKeyEventSink(
        m_tid, static_cast<ITfKeyEventSink*>(this), TRUE /* fForeground */));

    pKeyMgr->Release();
    return ok;
}

void CTextService::UninitKeyEventSink()
{
    if (!m_pThreadMgr || m_tid == TF_CLIENTID_NULL)
        return;

    ITfKeystrokeMgr* pKeyMgr = nullptr;
    if (SUCCEEDED(m_pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, reinterpret_cast<void**>(&pKeyMgr))))
    {
        pKeyMgr->UnadviseKeyEventSink(m_tid);
        pKeyMgr->Release();
    }
}

// ---------------------------------------------------------------------------
// ITfThreadMgrEventSink (mostly no-ops for this minimal service)
// ---------------------------------------------------------------------------

STDMETHODIMP CTextService::OnInitDocumentMgr(ITfDocumentMgr*)   { return S_OK; }
STDMETHODIMP CTextService::OnUninitDocumentMgr(ITfDocumentMgr*) { return S_OK; }
STDMETHODIMP CTextService::OnPushContext(ITfContext*)           { return S_OK; }
STDMETHODIMP CTextService::OnPopContext(ITfContext*)            { return S_OK; }

STDMETHODIMP CTextService::OnSetFocus(ITfDocumentMgr* /*pdimFocus*/, ITfDocumentMgr* /*pdimPrev*/)
{
    // Focus moved to a different document: hide UI and drop any buffer. We do
    // not try to carry a composition across documents in this sample.
    RemoveWheelHook();
    ClearContext();
    m_candWnd.Hide();
    m_romanization.clear();
    m_candidates.clear();
    ResetSelection();
    m_twoD = false;
    if (m_pComposition)
    {
        m_pComposition->Release();
        m_pComposition = nullptr;
    }
    return S_OK;
}

// ---------------------------------------------------------------------------
// ITfKeyEventSink
// ---------------------------------------------------------------------------

STDMETHODIMP CTextService::OnSetFocus(BOOL fForeground)
{
    // Re-read settings on gaining focus so "Keyboard options" changes (dictionary
    // choice, font size) take effect without a sign-out.
    if (fForeground)
        ApplySettings();
    return S_OK;
}

// Returns true if wParam is an ASCII letter key (VK 'A'..'Z'). Note: TSF gives
// us virtual key codes; letters map 1:1 to uppercase ASCII.
static bool IsLetterVK(WPARAM vk)
{
    return vk >= 'A' && vk <= 'Z';
}

static bool IsDigit1to9VK(WPARAM vk, int& digit)
{
    if (vk >= '1' && vk <= '9')
    {
        digit = static_cast<int>(vk - '1'); // 0-based index
        return true;
    }
    // numpad
    if (vk >= VK_NUMPAD1 && vk <= VK_NUMPAD9)
    {
        digit = static_cast<int>(vk - VK_NUMPAD1);
        return true;
    }
    return false;
}

// Shift state check: we only treat plain letters (no Ctrl/Alt) as romanization input.
static bool CtrlOrAltDown()
{
    return (GetKeyState(VK_CONTROL) & 0x8000) ||
           (GetKeyState(VK_MENU)    & 0x8000);
}

// TSF may report Shift as VK_SHIFT or the sided VK_LSHIFT / VK_RSHIFT.
static bool IsShiftVK(WPARAM vk)
{
    return vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT;
}

// Decides whether we would consume a given key, WITHOUT mutating state.
STDMETHODIMP CTextService::OnTestKeyDown(ITfContext* /*pic*/, WPARAM wParam, LPARAM /*lParam*/, BOOL* pfEaten)
{
    if (pfEaten == nullptr)
        return E_INVALIDARG;
    *pfEaten = FALSE;

    if (CtrlOrAltDown())
        return S_OK; // never intercept shortcuts

    // English mode: intercept nothing; letters pass straight through.
    if (!m_chineseMode)
        return S_OK;

    if (IsComposing())
    {
        int d;
        if (IsLetterVK(wParam) ||
            wParam == VK_BACK || wParam == VK_RETURN ||
            wParam == VK_ESCAPE || wParam == VK_SPACE ||
            wParam == VK_UP || wParam == VK_DOWN ||
            wParam == VK_LEFT || wParam == VK_RIGHT ||
            wParam == VK_PRIOR || wParam == VK_NEXT ||
            IsDigit1to9VK(wParam, d))
        {
            *pfEaten = TRUE;
        }
        // Other keys: we will commit and pass through -> report NOT eaten so
        // the app still receives the key. (We commit in OnKeyDown.)
    }
    else
    {
        if (IsLetterVK(wParam))
            *pfEaten = TRUE;
    }
    return S_OK;
}

STDMETHODIMP CTextService::OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM /*lParam*/, BOOL* pfEaten)
{
    if (pfEaten == nullptr)
        return E_INVALIDARG;
    *pfEaten = FALSE;

    if (CtrlOrAltDown())
        return S_OK;

    // English mode: never consume keys (mirrors OnTestKeyDown).
    if (!m_chineseMode)
        return S_OK;

    if (!IsComposing())
    {
        if (IsLetterVK(wParam))
        {
            // Begin a new composition.
            wchar_t ch = static_cast<wchar_t>(towlower(static_cast<wint_t>(wParam)));
            m_romanization.assign(1, ch);
            StartOrUpdateComposition(pic);
            UpdateCandidates(pic);
            *pfEaten = TRUE;
        }
        return S_OK;
    }

    // ---- Composing ----
    int digit = 0;

    if (IsLetterVK(wParam))
    {
        wchar_t ch = static_cast<wchar_t>(towlower(static_cast<wint_t>(wParam)));
        m_romanization.push_back(ch);
        StartOrUpdateComposition(pic);
        UpdateCandidates(pic);
        *pfEaten = TRUE;
    }
    else if (wParam == VK_BACK)
    {
        if (!m_romanization.empty())
            m_romanization.pop_back();
        if (m_romanization.empty())
            CancelComposition(pic);
        else
        {
            StartOrUpdateComposition(pic);
            UpdateCandidates(pic);
        }
        *pfEaten = TRUE;
    }
    else if (wParam == VK_ESCAPE)
    {
        CancelComposition(pic);
        *pfEaten = TRUE;
    }
    else if (wParam == VK_RETURN || wParam == VK_SPACE)
    {
        // Enter and Space both accept the currently selected candidate.
        if (!m_candidates.empty())
            CommitCandidate(pic, m_sel);
        else
            CommitRaw(pic);
        *pfEaten = TRUE;
    }
    else if (wParam == VK_DOWN || wParam == VK_UP ||
             wParam == VK_LEFT || wParam == VK_RIGHT ||
             wParam == VK_NEXT || wParam == VK_PRIOR)
    {
        // Selection / paging navigation. Always eaten while composing so the
        // app never sees these keys; they simply do nothing with no candidates.
        if (!m_candidates.empty())
        {
            switch (wParam)
            {
            case VK_DOWN:  SelDown();  break;
            case VK_UP:    SelUp();    break;
            case VK_RIGHT: SelRight(); break;
            case VK_LEFT:  SelLeft();  break;
            case VK_NEXT:  PageDown(); break; // PgDn
            case VK_PRIOR: PageUp();   break; // PgUp
            }
            RefreshCandView();
        }
        *pfEaten = TRUE;
    }
    else if (IsDigit1to9VK(wParam, digit))
    {
        // Number keys accept the candidate in that row of the SELECTED page
        // (column), not the global list.
        size_t idx = SelectedPageBase() + static_cast<size_t>(digit);
        if (idx < m_candidates.size())
        {
            CommitCandidate(pic, idx);
            *pfEaten = TRUE;
        }
        else
        {
            // No candidate at that slot: commit the selection, let digit through.
            if (!m_candidates.empty())
                CommitCandidate(pic, m_sel);
            else
                CommitRaw(pic);
            *pfEaten = FALSE;
        }
    }
    else
    {
        // Any other key: commit the selection, then let the key pass through.
        if (!m_candidates.empty())
            CommitCandidate(pic, m_sel);
        else
            CommitRaw(pic);
        *pfEaten = FALSE;
    }

    return S_OK;
}

STDMETHODIMP CTextService::OnTestKeyUp(ITfContext*, WPARAM, LPARAM, BOOL* pfEaten)
{
    // The lone-Shift mode toggle is handled by a low-level keyboard hook, not
    // here: TSF does not reliably deliver modifier-only keys to this sink.
    if (pfEaten) *pfEaten = FALSE;
    return S_OK;
}

STDMETHODIMP CTextService::OnKeyUp(ITfContext*, WPARAM, LPARAM, BOOL* pfEaten)
{
    if (pfEaten) *pfEaten = FALSE;
    return S_OK;
}

STDMETHODIMP CTextService::OnPreservedKey(ITfContext*, REFGUID, BOOL* pfEaten)
{
    if (pfEaten) *pfEaten = FALSE;
    return S_OK;
}

// ---------------------------------------------------------------------------
// ITfCompositionSink
// ---------------------------------------------------------------------------

STDMETHODIMP CTextService::OnCompositionTerminated(TfEditCookie /*ec*/, ITfComposition* /*pComposition*/)
{
    // The system (e.g. focus change, mouse click) ended our composition.
    // Release our reference and clear UI/state.
    RemoveWheelHook();
    ClearContext();
    m_candWnd.Hide();
    m_romanization.clear();
    m_candidates.clear();
    ResetSelection();
    m_twoD = false;
    if (m_pComposition)
    {
        m_pComposition->Release();
        m_pComposition = nullptr;
    }
    return S_OK;
}

// ---------------------------------------------------------------------------
// Candidate management
// ---------------------------------------------------------------------------

// Upper bound on candidates we build for one buffer. Prefixes like "a" can
// match thousands of keys; the window pages through them 9 at a time, but we
// still cap the working set so a broad prefix stays responsive.
static const size_t kMaxCandidates = 300;
static const size_t kMaxPrefixKeys = 200;

// Builds m_candidates from m_romanization, preferring segmentations that consume
// the buffer greedily: the longest key first, then the longest next key, etc.
//
// The buffer is split into a run of leading exact keys (each locked to its best
// character) followed by a non-empty final segment that is prefix-matched. Every
// such split is enumerated, then ordered by its per-key consumption sequence
// (leading key lengths, then the final segment's length) compared largest-first.
// So for "abcdefg" with keys abc/def/g/abcd/efg, "abcd efg" (lengths 4,3) beats
// "abc def g" (lengths 3,3,1) because its first key is longer, not because it
// uses fewer keys. The 0-leading-key split is the whole buffer prefix-matched:
// its first key consumes all n characters, the maximum possible, so a real key
// spanning the buffer still ranks first, e.g. "hah" surfaces 哈哈 (key "haha")
// before 下下 (a doubling of "ha").
//
// Each split emits one entry per character of each prefix-matched key, displayed
// as <locked chars><char><leftover romanization> and committed as <locked chars>
// <char>. All splits share one dedup set keyed on the commit string, so the first
// (greediest) segmentation to produce a given Chinese text wins and later
// duplicates drop.
void CTextService::BuildCandidates()
{
    m_candidates.clear();
    if (m_romanization.empty())
        return;

    const std::wstring& buf = m_romanization;
    const size_t n = buf.size();

    std::unordered_set<std::wstring> seenCommits;

    // Emits candidates for one (locked prefix, open final segment) split.
    // Returns false once the candidate cap is reached so callers stop early.
    auto emit = [&](const std::wstring& locked, const std::wstring& finalSeg) -> bool
    {
        std::vector<std::wstring> keys = m_dict.KeysWithPrefix(finalSeg, kMaxPrefixKeys);
        for (const std::wstring& key : keys)
        {
            std::wstring remainder = key.substr(finalSeg.size()); // "" for exact match
            for (const std::wstring& ch : m_dict.Lookup(key))
            {
                std::wstring commit = locked + ch;
                if (!seenCommits.insert(commit).second)
                    continue; // already emitted this text via a greedier split
                Candidate c;
                c.display = commit + remainder;
                c.commit  = std::move(commit);
                m_candidates.push_back(std::move(c));
                if (m_candidates.size() >= kMaxCandidates)
                    return false;
            }
        }
        return true;
    };

    // ---- Enumerate every split: partition a prefix [0, consumed) of the buffer
    // into leading exact keys (locked to their best character), leaving a
    // non-empty final segment [consumed, n) to prefix-match. `seq` is the split's
    // consumption profile: the leading key lengths followed by the final segment
    // length (always summing to n). ----
    struct Split { std::wstring locked; size_t consumed; std::vector<size_t> seq; };
    std::vector<Split> splits;
    const size_t kMaxSplits = 256; // safety valve against pathological fan-out

    std::function<void(size_t, const std::wstring&, const std::vector<size_t>&)> gather =
        [&](size_t pos, const std::wstring& locked, const std::vector<size_t>& lens)
    {
        if (splits.size() >= kMaxSplits)
            return;
        std::vector<size_t> seq = lens;
        seq.push_back(n - pos);                       // the final segment's length
        splits.push_back({ locked, pos, std::move(seq) }); // pos < n

        // Extend by one more leading exact key, keeping the final segment
        // non-empty (pos + len < n). Lengths are bounded by the longest exact
        // key at this position.
        size_t maxLen = m_dict.LongestExactPrefixLen(buf, pos);
        for (size_t len = 1; len <= maxLen && pos + len < n; ++len)
        {
            std::wstring key = buf.substr(pos, len);
            const auto& chars = m_dict.Lookup(key);
            if (chars.empty())
                continue; // not an exact key at this length
            std::vector<size_t> nextLens = lens;
            nextLens.push_back(len);
            gather(pos + len, locked + chars.front(), nextLens);
        }
    };
    gather(0, std::wstring(), std::vector<size_t>());

    // Greediest consumption first: compare the length sequences element by
    // element, larger length wins. stable_sort keeps discovery order on ties.
    std::stable_sort(splits.begin(), splits.end(),
        [](const Split& a, const Split& b)
        {
            size_t m = std::min(a.seq.size(), b.seq.size());
            for (size_t i = 0; i < m; ++i)
                if (a.seq[i] != b.seq[i])
                    return a.seq[i] > b.seq[i];
            return a.seq.size() < b.seq.size();
        });

    for (const Split& s : splits)
        if (!emit(s.locked, buf.substr(s.consumed)))
            return;
}

void CTextService::UpdateCandidates(ITfContext* pic)
{
    // Remember the context so click/wheel handlers can commit into it.
    SetContext(pic);

    BuildCandidates();

    // The list just changed; selection returns to the first candidate. The 2D
    // latch (m_twoD) persists for the whole composition, as specified.
    ResetSelection();

    if (m_candidates.empty())
    {
        RemoveWheelHook();
        m_candWnd.Hide();
        return;
    }

    std::vector<std::wstring> displays;
    displays.reserve(m_candidates.size());
    for (const Candidate& c : m_candidates)
        displays.push_back(c.display);

    m_candWnd.SetCandidates(displays);
    m_candWnd.SetView(m_pageBase, m_sel, m_twoD, kMaxCols);
    MoveCandidateWindowToCaret(pic);

    // Wheel ticks act as up/down; install the hook while the list is shown.
    InstallWheelHook();
}

// Positions the candidate window just below the caret using
// ITfContextView::GetTextExt. Falls back to the current cursor position.
void CTextService::MoveCandidateWindowToCaret(ITfContext* pic)
{
    POINT pt = { 0, 0 };
    bool havePoint = false;

    if (pic && m_pComposition)
    {
        ITfContextView* pView = nullptr;
        if (SUCCEEDED(pic->GetActiveView(&pView)) && pView)
        {
            // We need the range of the composition to ask for its screen rect.
            ITfRange* pRange = nullptr;
            if (SUCCEEDED(m_pComposition->GetRange(&pRange)) && pRange)
            {
                // GetTextExt must run inside a read edit session to have a valid
                // cookie. We use a lightweight synchronous session here.
                struct Ctx { ITfContextView* v; ITfRange* r; RECT rc; BOOL clipped; HRESULT hr; } c
                    { pView, pRange, {0,0,0,0}, FALSE, E_FAIL };

                CEditSession* sess = new CEditSession(pic,
                    [&c](TfEditCookie ec) -> HRESULT
                    {
                        c.hr = c.v->GetTextExt(ec, c.r, &c.rc, &c.clipped);
                        return c.hr;
                    });

                HRESULT hrSession = E_FAIL;
                pic->RequestEditSession(m_tid, sess,
                                        TF_ES_READ | TF_ES_SYNC, &hrSession);
                sess->Release();

                if (SUCCEEDED(c.hr))
                {
                    // Place the window just below the caret rect.
                    pt.x = c.rc.left;
                    pt.y = c.rc.bottom + 2;
                    havePoint = true;
                }
                pRange->Release();
            }
            pView->Release();
        }
    }

    if (!havePoint)
    {
        // Fallback: use the system caret / cursor position.
        POINT cur;
        if (GetCaretPos(&cur))
        {
            // GetCaretPos is window-relative; best-effort convert using the
            // foreground window. If that fails, use the mouse cursor.
            HWND hFore = GetForegroundWindow();
            if (hFore && ClientToScreen(hFore, &cur))
            {
                pt = cur;
                pt.y += 20;
                havePoint = true;
            }
        }
        if (!havePoint && GetCursorPos(&cur))
        {
            pt = cur;
            pt.y += 20;
            havePoint = true;
        }
    }

    m_candWnd.Show(pt);
}

// ---------------------------------------------------------------------------
// Composition operations (each runs its own edit session)
// ---------------------------------------------------------------------------

void CTextService::RequestEditSession(ITfContext* pic, EditAction action, const std::wstring& text)
{
    if (!pic)
        return;

    CEditSession* sess = new CEditSession(pic,
        [this, pic, action, text](TfEditCookie ec) -> HRESULT
        {
            switch (action)
            {
            case EditAction::Start:  return DoStartComposition(ec, pic);
            case EditAction::Update: return DoUpdateComposition(ec, pic, text);
            case EditAction::Commit: return DoCommitText(ec, pic, text);
            case EditAction::Cancel: return DoCancelComposition(ec, pic);
            }
            return S_OK;
        });

    HRESULT hrSession = E_FAIL;
    pic->RequestEditSession(m_tid, sess, TF_ES_READWRITE | TF_ES_SYNC, &hrSession);
    sess->Release();
}

void CTextService::StartOrUpdateComposition(ITfContext* pic)
{
    if (!IsComposing())
        RequestEditSession(pic, EditAction::Start, m_romanization);
    else
        RequestEditSession(pic, EditAction::Update, m_romanization);
}

void CTextService::CommitCandidate(ITfContext* pic, size_t index)
{
    if (index >= m_candidates.size())
        return;
    std::wstring ch = m_candidates[index].commit;
    RequestEditSession(pic, EditAction::Commit, ch);
    EndSelectionSession();
}

void CTextService::CommitRaw(ITfContext* pic)
{
    std::wstring raw = m_romanization;
    RequestEditSession(pic, EditAction::Commit, raw);
    EndSelectionSession();
}

void CTextService::CancelComposition(ITfContext* pic)
{
    RequestEditSession(pic, EditAction::Cancel, std::wstring());
    EndSelectionSession();
}

// Shared teardown after a composition ends (commit/cancel): clear the buffer,
// candidate list, selection/paging latch, the wheel hook, and the remembered
// context, and hide the window.
void CTextService::EndSelectionSession()
{
    RemoveWheelHook();
    ClearContext();
    m_romanization.clear();
    m_candidates.clear();
    ResetSelection();
    m_twoD = false;
    m_candWnd.Hide();
}

// ---------------------------------------------------------------------------
// Candidate selection / paging
// ---------------------------------------------------------------------------

void CTextService::ResetSelection()
{
    m_sel = 0;
    m_pageBase = 0;
}

// Number of candidate columns currently displayed from m_pageBase. Always 1 in
// 1D mode. In 2D mode the count is whatever the window can actually fit on
// screen (capped by kMaxCols and the monitor width), queried from the window so
// navigation never lands on a column that was dropped for width.
size_t CTextService::ColsShown() const
{
    if (!m_twoD)
        return 1;
    return m_candWnd.ColumnsThatFit(m_pageBase, m_twoD, kMaxCols);
}

// First global index of the column/page that holds the current selection.
size_t CTextService::SelectedPageBase() const
{
    if (m_candidates.empty())
        return 0;
    size_t col = m_twoD ? (m_sel - m_pageBase) / kPageSize : 0;
    return m_pageBase + col * kPageSize;
}

void CTextService::SelDown()
{
    size_t n = m_candidates.size();
    if (n == 0) return;

    if (!m_twoD)
    {
        // Move down within the page; at the 9th, reveal the next page.
        if (m_sel + 1 < n && m_sel + 1 < m_pageBase + kPageSize)
            ++m_sel;
        else if (m_pageBase + kPageSize < n)
        {
            m_pageBase += kPageSize;
            m_sel = m_pageBase;
        }
    }
    else
    {
        // 2D: move within the current column only (no vertical paging).
        size_t row = (m_sel - m_pageBase) % kPageSize;
        if (row + 1 < kPageSize && m_sel + 1 < n)
            ++m_sel;
    }
}

void CTextService::SelUp()
{
    if (m_candidates.empty()) return;

    if (!m_twoD)
    {
        if (m_sel > m_pageBase)
            --m_sel;
        else if (m_pageBase >= kPageSize)
        {
            m_pageBase -= kPageSize;
            m_sel = m_pageBase + kPageSize - 1; // last candidate of the prev page
        }
    }
    else
    {
        size_t row = (m_sel - m_pageBase) % kPageSize;
        if (row > 0)
            --m_sel;
    }
}

void CTextService::SelRight()
{
    size_t n = m_candidates.size();
    if (n == 0) return;

    // First right-arrow latches 2D mode for the rest of the composition. The
    // extra columns simply appear; the selection stays where it was.
    if (!m_twoD)
    {
        m_twoD = true;
        return;
    }

    // Move to the same row of the next column, if one exists, then scroll so the
    // selection is on screen. Columns are aligned to kPageSize globally, so the
    // selection's column starts at (m_sel / kPageSize) * kPageSize.
    size_t colStart = (m_sel / kPageSize) * kPageSize;
    size_t row      = m_sel - colStart;
    size_t nextCol  = colStart + kPageSize;
    if (nextCol >= n)
        return; // no column to the right
    size_t target = nextCol + row;
    if (target >= n) target = n - 1; // clamp into a partial column
    m_sel = target;
    ScrollSelectionIntoView();
}

void CTextService::SelLeft()
{
    if (m_candidates.empty()) return;
    if (!m_twoD) return; // left has no effect before 2D is engaged

    size_t colStart = (m_sel / kPageSize) * kPageSize;
    if (colStart < kPageSize)
        return; // already in the leftmost column
    m_sel -= kPageSize; // same row, previous column
    ScrollSelectionIntoView();
}

// Adjusts m_pageBase so the selected candidate's column is within the columns
// the window actually shows. The visible column count is width-capped and
// depends on m_pageBase, so scroll one column at a time and re-query until the
// selection fits; ColsShown() is always >= 1, so this terminates.
void CTextService::ScrollSelectionIntoView()
{
    size_t colStart = (m_sel / kPageSize) * kPageSize;

    // Selection left of the viewport: make its column the leftmost.
    if (colStart < m_pageBase)
    {
        m_pageBase = colStart;
        return;
    }
    // Selection past the last visible column: advance the viewport until it fits.
    while (colStart >= m_pageBase + ColsShown() * kPageSize)
        m_pageBase += kPageSize;
}

void CTextService::PageDown()
{
    size_t n = m_candidates.size();
    if (n == 0) return;

    size_t delta = m_twoD ? ColsShown() * kPageSize : kPageSize;
    if (m_pageBase + delta < n)
    {
        m_pageBase += delta;
        m_sel = m_pageBase; // reset to the first candidate of the new page
    }
}

void CTextService::PageUp()
{
    if (m_candidates.empty()) return;

    size_t delta = m_twoD ? ColsShown() * kPageSize : kPageSize;
    if (m_pageBase >= delta)
        m_pageBase -= delta;
    else
        m_pageBase = 0;
    m_sel = m_pageBase;
}

void CTextService::RefreshCandView()
{
    m_candWnd.SetView(m_pageBase, m_sel, m_twoD, kMaxCols);
}

// ---------------------------------------------------------------------------
// Mouse click / wheel
// ---------------------------------------------------------------------------

void CTextService::OnCandidateClicked(size_t globalIndex)
{
    if (!m_pContext || globalIndex >= m_candidates.size())
        return;
    CommitCandidate(m_pContext, globalIndex);
}

void CTextService::OnWheel(int ticks)
{
    if (m_candidates.empty() || ticks == 0)
        return;
    int steps = ticks < 0 ? -ticks : ticks;
    for (int i = 0; i < steps; ++i)
    {
        if (ticks > 0) SelUp();   // wheel up  -> up arrow
        else           SelDown(); // wheel down -> down arrow
    }
    RefreshCandView();
}

// ---------------------------------------------------------------------------
// Context bookkeeping + wheel hook
// ---------------------------------------------------------------------------

void CTextService::SetContext(ITfContext* pic)
{
    if (m_pContext == pic)
        return;
    if (pic)
        pic->AddRef();
    if (m_pContext)
        m_pContext->Release();
    m_pContext = pic;
}

void CTextService::ClearContext()
{
    if (m_pContext)
    {
        m_pContext->Release();
        m_pContext = nullptr;
    }
}

void CTextService::InstallWheelHook()
{
    s_wheelOwner = this;
    if (s_wheelHook)
        return; // already installed
    s_wheelHook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, g_hInst, 0);
}

void CTextService::RemoveWheelHook()
{
    if (s_wheelHook)
    {
        UnhookWindowsHookEx(s_wheelHook);
        s_wheelHook = nullptr;
    }
    if (s_wheelOwner == this)
        s_wheelOwner = nullptr;
}

LRESULT CALLBACK CTextService::LowLevelMouseProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && wParam == WM_MOUSEWHEEL && s_wheelOwner)
    {
        const MSLLHOOKSTRUCT* p = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
        // Only steer candidate selection when the cursor is actually over the
        // list. Anywhere else the wheel scrolls the app underneath as usual.
        if (s_wheelOwner->m_candWnd.HitTestScreen(p->pt))
        {
            int delta = GET_WHEEL_DELTA_WPARAM(p->mouseData);
            int ticks = delta / WHEEL_DELTA;
            if (ticks == 0)
                ticks = (delta > 0) ? 1 : -1;
            s_wheelOwner->OnWheel(ticks);
            return 1; // eat: the wheel drives candidate selection over the list
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

// ---- These four run INSIDE a granted edit cookie ----

HRESULT CTextService::DoStartComposition(TfEditCookie ec, ITfContext* pic)
{
    // Insert at the current selection and start a composition around it.
    ITfInsertAtSelection* pInsert = nullptr;
    if (FAILED(pic->QueryInterface(IID_ITfInsertAtSelection, reinterpret_cast<void**>(&pInsert))))
        return E_FAIL;

    ITfRange* pRange = nullptr;
    HRESULT hr = pInsert->InsertTextAtSelection(
        ec, TF_IAS_QUERYONLY, nullptr, 0, &pRange);
    if (FAILED(hr) || pRange == nullptr)
    {
        pInsert->Release();
        return E_FAIL;
    }

    ITfContextComposition* pComposeCtx = nullptr;
    if (SUCCEEDED(pic->QueryInterface(IID_ITfContextComposition, reinterpret_cast<void**>(&pComposeCtx))))
    {
        ITfComposition* pComposition = nullptr;
        hr = pComposeCtx->StartComposition(
            ec, pRange, static_cast<ITfCompositionSink*>(this), &pComposition);
        if (SUCCEEDED(hr) && pComposition)
        {
            m_pComposition = pComposition; // keep our reference
        }
        pComposeCtx->Release();
    }

    // Now write the initial buffer text into the composition range.
    if (m_pComposition)
        DoUpdateComposition(ec, pic, m_romanization);

    pRange->Release();
    pInsert->Release();
    return S_OK;
}

HRESULT CTextService::DoUpdateComposition(TfEditCookie ec, ITfContext* pic, const std::wstring& text)
{
    if (!m_pComposition)
        return S_OK;

    ITfRange* pRange = nullptr;
    if (FAILED(m_pComposition->GetRange(&pRange)) || pRange == nullptr)
        return E_FAIL;

    // Replace the composition range's text with the current buffer.
    HRESULT hr = pRange->SetText(ec, 0, text.c_str(), static_cast<LONG>(text.length()));

    // Move the caret to the end of the composition so the user sees a caret
    // after the displayed romanization.
    if (SUCCEEDED(hr))
    {
        ITfRange* pEnd = nullptr;
        if (SUCCEEDED(pRange->Clone(&pEnd)) && pEnd)
        {
            pEnd->Collapse(ec, TF_ANCHOR_END);
            TF_SELECTION sel;
            sel.range = pEnd;
            sel.style.ase = TF_AE_NONE;
            sel.style.fInterimChar = FALSE;
            pic->SetSelection(ec, 1, &sel);
            pEnd->Release();
        }
    }

    pRange->Release();
    return hr;
}

HRESULT CTextService::DoCommitText(TfEditCookie ec, ITfContext* pic, const std::wstring& text)
{
    // Put the committed text into the composition range, move the caret past
    // it, then end the composition so the text becomes permanent document text.
    if (m_pComposition)
    {
        ITfRange* pRange = nullptr;
        if (SUCCEEDED(m_pComposition->GetRange(&pRange)) && pRange)
        {
            pRange->SetText(ec, 0, text.c_str(), static_cast<LONG>(text.length()));

            ITfRange* pEnd = nullptr;
            if (SUCCEEDED(pRange->Clone(&pEnd)) && pEnd)
            {
                pEnd->Collapse(ec, TF_ANCHOR_END);
                TF_SELECTION sel;
                sel.range = pEnd;
                sel.style.ase = TF_AE_NONE;
                sel.style.fInterimChar = FALSE;
                pic->SetSelection(ec, 1, &sel);
                pEnd->Release();
            }
            pRange->Release();
        }

        m_pComposition->EndComposition(ec);
        m_pComposition->Release();
        m_pComposition = nullptr;
    }
    return S_OK;
}

HRESULT CTextService::DoCancelComposition(TfEditCookie ec, ITfContext* pic)
{
    // Clear the composition range's text, then end the composition. This leaves
    // the document as if nothing was typed.
    if (m_pComposition)
    {
        ITfRange* pRange = nullptr;
        if (SUCCEEDED(m_pComposition->GetRange(&pRange)) && pRange)
        {
            pRange->SetText(ec, 0, L"", 0);
            pRange->Release();
        }
        m_pComposition->EndComposition(ec);
        m_pComposition->Release();
        m_pComposition = nullptr;
    }
    return S_OK;
}

void CTextService::EndComposition(ITfContext* pic)
{
    CancelComposition(pic);
}

// ---------------------------------------------------------------------------
// Per-user settings (HKCU): candidate font size + selected dictionary
// ---------------------------------------------------------------------------

void CTextService::ApplySettings()
{
    CantoSettings s = LoadSettings();
    m_candWnd.SetFontSize(s.fontSize);

    // Reload the dictionary only if the resolved path actually changed.
    std::wstring path = ResolveDictionaryPath();
    if (path != m_dictPathLoaded)
    {
        m_dict.Load(path);
        m_dictPathLoaded = path;
    }
}

// ---------------------------------------------------------------------------
// ITfFunctionProvider + ITfFnConfigure: the "Keyboard options" dialog
//
// Windows opens the config dialog by CoCreate(CLSID, IID_ITfFnConfigure) using
// the CLSID registered via ITfInputProcessorProfiles::Register (see dllmain).
// It can also QI the function provider and GetFunction(GUID_NULL, ...). Show()
// stands alone (no active thread manager needed): it just opens the dialog.
// ---------------------------------------------------------------------------

STDMETHODIMP CTextService::GetType(GUID* pguid)
{
    if (pguid == nullptr)
        return E_INVALIDARG;
    *pguid = CLSID_CantoIME;
    return S_OK;
}

STDMETHODIMP CTextService::GetDescription(BSTR* pbstrDesc)
{
    if (pbstrDesc == nullptr)
        return E_INVALIDARG;
    *pbstrDesc = SysAllocString(TEXTSERVICE_DESC);
    return *pbstrDesc ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CTextService::GetFunction(REFGUID rguid, REFIID riid, IUnknown** ppunk)
{
    if (ppunk == nullptr)
        return E_INVALIDARG;
    *ppunk = nullptr;
    if (IsEqualGUID(rguid, GUID_NULL) && IsEqualIID(riid, IID_ITfFnConfigure))
    {
        *ppunk = static_cast<ITfFnConfigure*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP CTextService::GetDisplayName(BSTR* pbstrName)
{
    if (pbstrName == nullptr)
        return E_INVALIDARG;
    *pbstrName = SysAllocString(L"CantoIME Options");
    return *pbstrName ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CTextService::Show(HWND hwndParent, LANGID /*langid*/, REFGUID /*rguidProfile*/)
{
    ShowOptionsDialog(hwndParent);
    return S_OK;
}
