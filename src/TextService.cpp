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

#include <msctf.h>
#include <ctffunc.h>   // ITfContextView / display helpers live in msctf; kept for completeness
#include <windowsx.h>  // GET_WHEEL_DELTA_WPARAM
#include <unordered_set>
#include <functional>
#include <algorithm>

// Statics backing the low-level mouse wheel hook.
HHOOK         CTextService::s_wheelHook  = nullptr;
CTextService* CTextService::s_wheelOwner = nullptr;

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

    // Prepare (but do not show) the candidate window, and route candidate
    // clicks back to us so a mouse click accepts that candidate.
    m_candWnd.EnsureCreated();
    m_candWnd.SetOnAccept([this](size_t globalIndex)
    {
        OnCandidateClicked(globalIndex);
    });

    return S_OK;

fail:
    Deactivate();
    return E_FAIL;
}

STDMETHODIMP CTextService::Deactivate()
{
    // Ensure any in-flight composition is torn down and UI hidden.
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

// Shift state check: we only treat plain letters (no Ctrl/Alt) as romanization input.
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

    size_t col  = (m_sel - m_pageBase) / kPageSize;
    size_t cols = ColsShown();
    if (col + 1 < cols)
    {
        size_t target = m_sel + kPageSize;      // same row, next column
        if (target >= n) target = n - 1;        // clamp into a partial column
        m_sel = target;
    }
    else if (m_pageBase + cols * kPageSize < n)
    {
        // At the rightmost visible column: scroll the grid one column right.
        m_pageBase += kPageSize;
        size_t target = m_sel + kPageSize;
        if (target >= n) target = n - 1;
        m_sel = target;
    }
}

void CTextService::SelLeft()
{
    if (m_candidates.empty()) return;
    if (!m_twoD) return; // left has no effect before 2D is engaged

    size_t col = (m_sel - m_pageBase) / kPageSize;
    if (col > 0)
    {
        m_sel -= kPageSize; // same row, previous column
    }
    else if (m_pageBase >= kPageSize)
    {
        // Leftmost column: scroll the grid one column left.
        m_pageBase -= kPageSize;
        m_sel      -= kPageSize;
    }
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
