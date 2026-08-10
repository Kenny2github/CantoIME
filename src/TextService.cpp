// TextService.cpp
//
// Implementation of the core TSF text service.
//
// KEY HANDLING SUMMARY (documented behavior):
//   While NOT composing:
//     - a-z         : start a composition, begin the pinyin buffer, eat key.
//     - anything    : pass through (not eaten).
//   While composing:
//     - a-z         : append to buffer, refresh candidates, eat key.
//     - Backspace   : remove last letter; if buffer becomes empty, cancel
//                     composition. Eat key. (If not composing, pass through.)
//     - 1..9        : if a candidate exists at that index, commit it, eat key;
//                     otherwise the digit is treated as a normal keystroke and
//                     appended is NOT done (digits are never part of pinyin),
//                     so we commit the raw buffer and let the digit pass.
//     - Space       : commit first candidate; if none, commit raw buffer.
//     - Enter       : commit the raw typed pinyin text as-is.
//     - Esc         : cancel composition, clear buffer, eat key.
//     - Any other    : SIMPLEST COHERENT CHOICE -> commit the current best
//                     result (first candidate if any, else raw buffer), then
//                     let the key pass through to the app (not eaten). This
//                     matches how most IMEs behave when you type punctuation
//                     mid-composition.

#include "TextService.h"
#include "EditSession.h"

#include <msctf.h>
#include <ctffunc.h>   // ITfContextView / display helpers live in msctf; kept for completeness

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

    // Load the dictionary at activation using the documented search order.
    m_dict.Load(ResolveDictionaryPath());

    // Prepare (but do not show) the candidate window.
    m_candWnd.EnsureCreated();

    return S_OK;

fail:
    Deactivate();
    return E_FAIL;
}

STDMETHODIMP CTextService::Deactivate()
{
    // Ensure any in-flight composition is torn down and UI hidden.
    m_candWnd.Hide();
    m_pinyin.clear();
    m_candidates.clear();

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
    m_candWnd.Hide();
    m_pinyin.clear();
    m_candidates.clear();
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

STDMETHODIMP CTextService::OnSetFocus(BOOL /*fForeground*/)
{
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

// Shift state check: we only treat plain letters (no Ctrl/Alt) as pinyin input.
static bool CtrlOrAltDown()
{
    return (GetKeyState(VK_CONTROL) & 0x8000) ||
           (GetKeyState(VK_MENU)    & 0x8000);
}

// Decides whether we would consume a given key, WITHOUT mutating state.
STDMETHODIMP CTextService::OnTestKeyDown(ITfContext* /*pic*/, WPARAM wParam, LPARAM /*lParam*/, BOOL* pfEaten)
{
    if (pfEaten == nullptr)
        return E_INVALIDARG;
    *pfEaten = FALSE;

    if (CtrlOrAltDown())
        return S_OK; // never intercept shortcuts

    if (IsComposing())
    {
        int d;
        if (IsLetterVK(wParam) ||
            wParam == VK_BACK || wParam == VK_RETURN ||
            wParam == VK_ESCAPE || wParam == VK_SPACE ||
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

    if (!IsComposing())
    {
        if (IsLetterVK(wParam))
        {
            // Begin a new composition.
            wchar_t ch = static_cast<wchar_t>(towlower(static_cast<wint_t>(wParam)));
            m_pinyin.assign(1, ch);
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
        m_pinyin.push_back(ch);
        StartOrUpdateComposition(pic);
        UpdateCandidates(pic);
        *pfEaten = TRUE;
    }
    else if (wParam == VK_BACK)
    {
        if (!m_pinyin.empty())
            m_pinyin.pop_back();
        if (m_pinyin.empty())
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
    else if (wParam == VK_RETURN)
    {
        // Commit the raw typed pinyin as-is.
        CommitRaw(pic);
        *pfEaten = TRUE;
    }
    else if (wParam == VK_SPACE)
    {
        if (!m_candidates.empty())
            CommitCandidate(pic, 0);
        else
            CommitRaw(pic);
        *pfEaten = TRUE;
    }
    else if (IsDigit1to9VK(wParam, digit))
    {
        if (digit < static_cast<int>(m_candidates.size()))
        {
            CommitCandidate(pic, static_cast<size_t>(digit));
            *pfEaten = TRUE;
        }
        else
        {
            // No candidate at that slot: commit best result, let digit through.
            if (!m_candidates.empty())
                CommitCandidate(pic, 0);
            else
                CommitRaw(pic);
            *pfEaten = FALSE;
        }
    }
    else
    {
        // Any other key: commit best result, then let the key pass through.
        if (!m_candidates.empty())
            CommitCandidate(pic, 0);
        else
            CommitRaw(pic);
        *pfEaten = FALSE;
    }

    return S_OK;
}

STDMETHODIMP CTextService::OnTestKeyUp(ITfContext*, WPARAM, LPARAM, BOOL* pfEaten)
{
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
    m_candWnd.Hide();
    m_pinyin.clear();
    m_candidates.clear();
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

// Builds m_candidates from m_pinyin using greedy phrase segmentation:
//   * Lock complete leading syllables (each an exact dictionary key), choosing
//     each syllable's best (first) character.
//   * Prefix-match the final, still-open syllable. For every key that begins
//     with it, emit one candidate per character: display = locked chars + char
//     + the key's leftover romanization; commit = locked chars + char.
//
// Example: "gaogaod" locks 高高 (gao, gao), then prefix-matches "d" -> "de"
// giving display "高高的e", commit "高高的". "gagad" locks nothing ("ga" is not
// an exact key) and "gagad" prefix-matches no key, so no candidates appear.
void CTextService::BuildCandidates()
{
    m_candidates.clear();
    if (m_pinyin.empty())
        return;

    // ---- Lock complete leading syllables, leaving the final one open. ----
    std::wstring locked;   // committed Chinese for the fixed leading syllables
    size_t i = 0;
    while (i < m_pinyin.size())
    {
        size_t len = m_dict.LongestExactPrefixLen(m_pinyin, i);
        // Never lock the trailing syllable: it must stay open for prefix
        // matching (a fully typed final syllable is still a prefix of itself).
        if (len == 0 || i + len >= m_pinyin.size())
            break;

        const auto& chars = m_dict.Lookup(m_pinyin.substr(i, len));
        if (chars.empty())
            break; // shouldn't happen (len came from an exact key), but be safe
        locked += chars.front();
        i += len;
    }

    std::wstring finalSyl = m_pinyin.substr(i);
    if (finalSyl.empty())
        return;

    // ---- Prefix-match the final syllable. ----
    std::vector<std::wstring> keys = m_dict.KeysWithPrefix(finalSyl, kMaxPrefixKeys);
    for (const std::wstring& key : keys)
    {
        std::wstring remainder = key.substr(finalSyl.size()); // "" for exact match
        for (const std::wstring& ch : m_dict.Lookup(key))
        {
            Candidate c;
            c.display = locked + ch + remainder;
            c.commit  = locked + ch;
            m_candidates.push_back(std::move(c));
            if (m_candidates.size() >= kMaxCandidates)
                return;
        }
    }
}

void CTextService::UpdateCandidates(ITfContext* pic)
{
    BuildCandidates();

    if (m_candidates.empty())
    {
        m_candWnd.Hide();
        return;
    }

    std::vector<std::wstring> displays;
    displays.reserve(m_candidates.size());
    for (const Candidate& c : m_candidates)
        displays.push_back(c.display);

    m_candWnd.SetCandidates(displays);
    MoveCandidateWindowToCaret(pic);
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
        RequestEditSession(pic, EditAction::Start, m_pinyin);
    else
        RequestEditSession(pic, EditAction::Update, m_pinyin);
}

void CTextService::CommitCandidate(ITfContext* pic, size_t index)
{
    if (index >= m_candidates.size())
        return;
    std::wstring ch = m_candidates[index].commit;
    RequestEditSession(pic, EditAction::Commit, ch);
    m_pinyin.clear();
    m_candidates.clear();
    m_candWnd.Hide();
}

void CTextService::CommitRaw(ITfContext* pic)
{
    std::wstring raw = m_pinyin;
    RequestEditSession(pic, EditAction::Commit, raw);
    m_pinyin.clear();
    m_candidates.clear();
    m_candWnd.Hide();
}

void CTextService::CancelComposition(ITfContext* pic)
{
    RequestEditSession(pic, EditAction::Cancel, std::wstring());
    m_pinyin.clear();
    m_candidates.clear();
    m_candWnd.Hide();
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
        DoUpdateComposition(ec, pic, m_pinyin);

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
    // after the displayed pinyin.
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
