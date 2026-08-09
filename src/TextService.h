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
// Composition editing (inserting the pinyin buffer, committing a candidate,
// cancelling) is performed inside edit sessions, since TSF requires document
// mutation to happen within an ITfEditSession granted a valid edit cookie.

#pragma once

#include "Globals.h"
#include "Dictionary.h"
#include "CandidateWindow.h"

#include <msctf.h>
#include <string>
#include <vector>

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

    // Key handling helpers (all issue edit sessions as needed).
    bool IsComposing() const { return m_pComposition != nullptr; }
    void UpdateCandidates(ITfContext* pic);
    void MoveCandidateWindowToCaret(ITfContext* pic);
    void StartOrUpdateComposition(ITfContext* pic);
    void CommitCandidate(ITfContext* pic, size_t index);
    void CommitRaw(ITfContext* pic);
    void CancelComposition(ITfContext* pic);
    void EndComposition(ITfContext* pic); // terminates cleanly, clears buffer

    // Requests a synchronous read/write edit session running the given lambda
    // wrapper. Implemented by a small ITfEditSession adapter in the .cpp.
    enum class EditAction { Start, Update, Commit, Cancel };
    void RequestEditSession(ITfContext* pic, EditAction action, const std::wstring& text);

    LONG m_cRef;

    ITfThreadMgr*   m_pThreadMgr;   // not owned beyond our AddRef
    TfClientId      m_tid;
    ITfComposition* m_pComposition; // active composition, or null

    DWORD m_dwThreadMgrEventSinkCookie;

    // The pinyin composition buffer (ASCII letters typed so far).
    std::wstring m_pinyin;

    // Candidates currently shown (looked up from m_pinyin).
    std::vector<std::wstring> m_candidates;

    Dictionary      m_dict;
    CandidateWindow m_candWnd;
};

// Creates an instance of the text service (used by the class factory).
HRESULT CreateTextServiceInstance(REFIID riid, void** ppv);
