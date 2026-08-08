// TextService.h
//
// The core TSF text service object.
//
// It implements the interfaces a keyboard-style TIP (Text Input Processor)
// needs:
//   * ITfTextInputProcessorEx  - activation lifecycle
//   * ITfThreadMgrEventSink    - focus / document manager events
//   * ITfKeyEventSink          - keyboard input (the heart of the IME)
//   * ITfCompositionSink       - notified when a composition is terminated
//
// Composition editing (inserting the romanization buffer, committing a candidate,
// cancelling) is performed inside edit sessions, since TSF requires document
// mutation to happen within an ITfEditSession granted a valid edit cookie.

#pragma once

#include "Globals.h"
#include "Dictionary.h"
#include "CandidateWindow.h"

#include <msctf.h>
#include <string>
#include <vector>

class CLangBarButton; // language-bar item buttons (see LangBarButton.h)

class CTextService final :
    public ITfTextInputProcessorEx,
    public ITfThreadMgrEventSink,
    public ITfKeyEventSink,
    public ITfCompositionSink
{
public:
    CTextService();

    // --- IUnknown -----------------------------------------------------------
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // --- ITfTextInputProcessor / Ex ----------------------------------------
    STDMETHODIMP Activate(ITfThreadMgr* ptim, TfClientId tid) override;
    STDMETHODIMP Deactivate() override;
    STDMETHODIMP ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD dwFlags) override;

    // --- ITfThreadMgrEventSink ---------------------------------------------
    STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr* pdim) override;
    STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr* pdim) override;
    STDMETHODIMP OnSetFocus(ITfDocumentMgr* pdimFocus, ITfDocumentMgr* pdimPrevFocus) override;
    STDMETHODIMP OnPushContext(ITfContext* pic) override;
    STDMETHODIMP OnPopContext(ITfContext* pic) override;

    // --- ITfKeyEventSink ----------------------------------------------------
    STDMETHODIMP OnSetFocus(BOOL fForeground) override;
    STDMETHODIMP OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnTestKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnPreservedKey(ITfContext* pic, REFGUID rguid, BOOL* pfEaten) override;

    // --- ITfCompositionSink -------------------------------------------------
    STDMETHODIMP OnCompositionTerminated(TfEditCookie ecWrite, ITfComposition* pComposition) override;

    // ----- Called by the edit sessions (friends via public helpers) ---------
    // These run inside a granted edit cookie.
    HRESULT DoStartComposition(TfEditCookie ec, ITfContext* pic);
    HRESULT DoUpdateComposition(TfEditCookie ec, ITfContext* pic, const std::wstring& text);
    HRESULT DoCommitText(TfEditCookie ec, ITfContext* pic, const std::wstring& text);
    HRESULT DoCancelComposition(TfEditCookie ec, ITfContext* pic);

    ITfComposition* Composition() const { return m_pComposition; }
    TfClientId ClientId() const { return m_tid; }

private:
    ~CTextService();

    // Sink advise/unadvise helpers.
    bool InitThreadMgrEventSink();
    void UninitThreadMgrEventSink();
    bool InitKeyEventSink();
    void UninitKeyEventSink();

    // A single candidate. `display` is what the candidate window shows (may
    // include a trailing romanization remainder, e.g. L"高o"); `commit` is the
    // Chinese text actually inserted when the candidate is accepted (L"高").
    struct Candidate
    {
        std::wstring display;
        std::wstring commit;
    };

    // Key handling helpers (all issue edit sessions as needed).
    bool IsComposing() const { return m_pComposition != nullptr; }
    void BuildCandidates();          // fills m_candidates from m_romanization
    void UpdateCandidates(ITfContext* pic);
    void MoveCandidateWindowToCaret(ITfContext* pic);
    void StartOrUpdateComposition(ITfContext* pic);
    void CommitCandidate(ITfContext* pic, size_t index);
    void CommitRaw(ITfContext* pic);
    void CancelComposition(ITfContext* pic);
    void EndComposition(ITfContext* pic); // terminates cleanly, clears buffer
    void EndSelectionSession();           // shared teardown after commit/cancel

    // --- Candidate selection / paging -------------------------------------
    // The window shows pages of kPageSize candidates. In 1D mode a single
    // column is shown and up/down page vertically at the boundaries. Once the
    // right arrow is pressed the view latches into 2D mode for the rest of the
    // composition: further pages appear as columns to the right and left/right
    // move between them.
    static const size_t kPageSize = CANTO_MAX_CANDIDATES; // 9
    static const size_t kMaxCols  = 5;                    // max width in 2D mode

    size_t ColsShown() const;      // columns currently displayed from m_pageBase
    void ResetSelection();         // selection -> first candidate, page -> 0
    void SelDown();                // vertical arrow / wheel down
    void SelUp();                  // vertical arrow / wheel up
    void SelRight();               // right arrow (latches 2D)
    void SelLeft();                // left arrow (2D only)
    void PageDown();               // PgDn
    void PageUp();                 // PgUp
    void RefreshCandView();        // push selection/paging state to the window
    size_t SelectedPageBase() const; // first index of the selected column/page
    void ScrollSelectionIntoView(); // move m_pageBase so m_sel's column is shown

    // Callbacks wired into the candidate window.
    void OnCandidateClicked(size_t globalIndex); // mouse click accepts
    void OnWheel(int ticks);                     // wheel ticks act as up/down

    // Remembers the context we are composing in, so click/wheel handlers (which
    // arrive outside a key event) can commit into the right document.
    void SetContext(ITfContext* pic);
    void ClearContext();

    // Low-level mouse hook so wheel ticks reach us even though the popup never
    // takes focus (and thus never receives WM_MOUSEWHEEL itself).
    void InstallWheelHook();
    void RemoveWheelHook();
    static LRESULT CALLBACK LowLevelMouseProc(int code, WPARAM wParam, LPARAM lParam);

    // Requests a synchronous read/write edit session running the given lambda
    // wrapper. Implemented by a small ITfEditSession adapter in the .cpp.
    enum class EditAction { Start, Update, Commit, Cancel };
    void RequestEditSession(ITfContext* pic, EditAction action, const std::wstring& text);

    LONG m_cRef;

    ITfThreadMgr*   m_pThreadMgr;   // not owned beyond our AddRef
    TfClientId      m_tid;
    ITfComposition* m_pComposition; // active composition, or null

    DWORD m_dwThreadMgrEventSinkCookie;

    // The romanization composition buffer (ASCII letters typed so far).
    std::wstring m_romanization;

    // Candidates currently shown (built from m_romanization).
    std::vector<Candidate> m_candidates;

    // Selection / paging state (see the helpers above).
    size_t m_sel      = 0;      // global index of the selected candidate
    size_t m_pageBase = 0;      // global index of the top-left displayed cell
    bool   m_twoD     = false;  // latched once the right arrow is pressed

    // Context we are currently composing in (AddRef'd), for click/wheel commits.
    ITfContext* m_pContext = nullptr;

    // English/Chinese input mode. Chinese (true) does romanization -> character
    // conversion; English (false) passes letters straight through. Toggled by a
    // lone Shift press/release or by clicking the input-mode language-bar button.
    bool m_chineseMode = true;

    void ToggleInputMode(); // flip Chinese/English, commit any composition first
    void RefreshLangBarIcons(); // re-query button icons (e.g. on a theme change)

    // Lone-Shift detection. TSF does not reliably deliver modifier-only keys to
    // the key-event sink, so a low-level keyboard hook watches Shift instead and
    // posts a toggle request to a message-only window (so the actual toggle runs
    // on the UI thread, not inside the hook callback).
    HWND  m_hMsgWnd     = nullptr;
    bool  m_kbShiftDown = false; // Shift is currently held
    bool  m_kbShiftLone = false; // no other key pressed since Shift went down
    void CreateMsgWindow();
    void DestroyMsgWindow();
    static LRESULT CALLBACK MsgWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void InstallKeyboardHook();
    void RemoveKeyboardHook();
    static LRESULT CALLBACK LowLevelKeyboardProc(int code, WPARAM wParam, LPARAM lParam);
    static HHOOK          s_kbdHook;
    static CTextService*  s_kbdOwner;

    // Language-bar items: the main input-method button (粵) and the input-mode
    // button (中/英). Registered in Activate, removed in Deactivate.
    void AddLangBarButtons();
    void RemoveLangBarButtons();
    CLangBarButton* m_pMainButton = nullptr;
    CLangBarButton* m_pModeButton = nullptr;

    Dictionary      m_dict;
    CandidateWindow m_candWnd;

    // Active service for the low-level mouse hook (one composition at a time).
    static HHOOK          s_wheelHook;
    static CTextService*  s_wheelOwner;
};

// Creates an instance of the text service (used by the class factory).
HRESULT CreateTextServiceInstance(REFIID riid, void** ppv);
