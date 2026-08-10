// EditSession.h
//
// A small reusable ITfEditSession adapter. TSF requires that any read/write of
// a document happen inside an edit session, so that TSF can serialize edits and
// hand us a valid edit cookie (TfEditCookie).
//
// Rather than write one class per operation, we use a single adapter that holds
// a std::function to run once the cookie is granted.

#pragma once

#include "Globals.h"
#include <msctf.h>
#include <functional>

class CEditSession final : public ITfEditSession
{
public:
    using Callback = std::function<HRESULT(TfEditCookie ec)>;

    CEditSession(ITfContext* pContext, Callback cb)
        : m_cRef(1), m_pContext(pContext), m_cb(std::move(cb))
    {
        if (m_pContext)
            m_pContext->AddRef();
    }

    // --- IUnknown ---
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (ppv == nullptr)
            return E_INVALIDARG;
        *ppv = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfEditSession))
        {
            *ppv = static_cast<ITfEditSession*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override
    {
        return InterlockedIncrement(&m_cRef);
    }

    STDMETHODIMP_(ULONG) Release() override
    {
        ULONG cr = InterlockedDecrement(&m_cRef);
        if (cr == 0)
            delete this;
        return cr;
    }

    // --- ITfEditSession ---
    STDMETHODIMP DoEditSession(TfEditCookie ec) override
    {
        if (m_cb)
            return m_cb(ec);
        return S_OK;
    }

private:
    ~CEditSession()
    {
        if (m_pContext)
            m_pContext->Release();
    }

    LONG        m_cRef;
    ITfContext* m_pContext;
    Callback    m_cb;
};
