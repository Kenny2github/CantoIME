// dllmain.cpp
//
// DLL entry point, COM class factory, and self-registration for the Minimal
// Pinyin IME text service.
//
// Registration performs three things:
//   1. Register the in-proc COM server (HKCR\CLSID\{clsid}\InprocServer32).
//   2. Register the TSF text service + language profile for LANGID 0x0404.
//   3. Register TSF categories, including the Windows-11-critical
//      IMMERSIVESUPPORT and SYSTRAYSUPPORT capabilities so the IME works in
//      modern (UWP / packaged / store) apps and the systray shows it.

#include "Globals.h"
#include "TextService.h"

#include <msctf.h>
#include <olectl.h>
#include <shlobj.h>
#include <new>
#include <string>
#include <cstdio>

// ---------------------------------------------------------------------------
// Registration diagnostic log
//
// regsvr32 only surfaces the top-level HRESULT (e.g. 0x80040201) and doesn't
// say which registration step failed. This appends every step + its HRESULT
// to %TEMP%\MinimalPinyinIME_reg.log so failures are debuggable from a bare
// Windows VM without a debugger attached.
// ---------------------------------------------------------------------------
static void RegLog(const wchar_t* fmt, ...)
{
    wchar_t path[MAX_PATH] = { 0 };
    if (GetTempPathW(MAX_PATH, path) == 0)
        return;
    if (wcslen(path) + 32 >= MAX_PATH)
        return;
    wcscat_s(path, MAX_PATH, L"MinimalPinyinIME_reg.log");

    FILE* fp = nullptr;
    if (_wfopen_s(&fp, path, L"a, ccs=UTF-8") != 0 || fp == nullptr)
        return;

    SYSTEMTIME st;
    GetLocalTime(&st);
    fwprintf(fp, L"[%04u-%02u-%02u %02u:%02u:%02u.%03u] ",
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    va_list ap;
    va_start(ap, fmt);
    vfwprintf(fp, fmt, ap);
    va_end(ap);

    fputwc(L'\n', fp);
    fclose(fp);

    // Also mirror to the debugger for live capture in DebugView.
    OutputDebugStringW(L"[MPIME] see %TEMP%\\MinimalPinyinIME_reg.log\n");
}

static bool IsProcessElevated()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    TOKEN_ELEVATION te = { 0 };
    DWORD ret = 0;
    bool elevated = false;
    if (GetTokenInformation(token, TokenElevation, &te, sizeof(te), &ret))
        elevated = (te.TokenIsElevated != 0);
    CloseHandle(token);
    return elevated;
}

// ---------------------------------------------------------------------------
// Class factory
// ---------------------------------------------------------------------------

class CClassFactory final : public IClassFactory
{
public:
    CClassFactory() : m_cRef(1) { DllAddRef(); }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (ppv == nullptr)
            return E_INVALIDARG;
        *ppv = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory))
        {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_cRef); }
    STDMETHODIMP_(ULONG) Release() override
    {
        ULONG cr = InterlockedDecrement(&m_cRef);
        if (cr == 0)
            delete this;
        return cr;
    }

    STDMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) override
    {
        if (ppv == nullptr)
            return E_INVALIDARG;
        *ppv = nullptr;
        if (pUnkOuter != nullptr)
            return CLASS_E_NOAGGREGATION; // no aggregation support
        return CreateTextServiceInstance(riid, ppv);
    }

    STDMETHODIMP LockServer(BOOL fLock) override
    {
        if (fLock)
            DllAddRef();
        else
            DllRelease();
        return S_OK;
    }

private:
    ~CClassFactory() { DllRelease(); }
    LONG m_cRef;
};

// ---------------------------------------------------------------------------
// DllMain
// ---------------------------------------------------------------------------

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID /*lpReserved*/)
{
    switch (dwReason)
    {
    case DLL_PROCESS_ATTACH:
        g_hInst = hInstance;
        DisableThreadLibraryCalls(hInstance);
        break;
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

// ---------------------------------------------------------------------------
// Standard COM in-proc exports
// ---------------------------------------------------------------------------

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv)
{
    if (ppv == nullptr)
        return E_INVALIDARG;
    *ppv = nullptr;

    if (!IsEqualCLSID(rclsid, CLSID_MinimalPinyinIME))
        return CLASS_E_CLASSNOTAVAILABLE;

    CClassFactory* pFactory = new (std::nothrow) CClassFactory();
    if (pFactory == nullptr)
        return E_OUTOFMEMORY;

    HRESULT hr = pFactory->QueryInterface(riid, ppv);
    pFactory->Release();
    return hr;
}

STDAPI DllCanUnloadNow()
{
    return (g_cRefDll <= 0) ? S_OK : S_FALSE;
}

// ---------------------------------------------------------------------------
// Registration helpers
// ---------------------------------------------------------------------------

// Converts a GUID to the canonical "{XXXXXXXX-XXXX-...}" string form.
static std::wstring GuidToString(REFGUID guid)
{
    wchar_t buf[64] = { 0 };
    StringFromGUID2(guid, buf, ARRAYSIZE(buf));
    return std::wstring(buf);
}

// Creates a key and sets its (default) value to the given string. Returns the
// Win32 error code (ERROR_SUCCESS on success) so callers can surface the real
// failure reason (e.g. ERROR_ACCESS_DENIED when not elevated).
static LONG SetRegKeyValue(HKEY root, const std::wstring& subkey,
                           const wchar_t* valueName, const std::wstring& data)
{
    HKEY hKey = nullptr;
    LONG rc = RegCreateKeyExW(root, subkey.c_str(), 0, nullptr,
                              REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    if (rc != ERROR_SUCCESS)
        return rc;

    rc = RegSetValueExW(hKey, valueName, 0, REG_SZ,
                        reinterpret_cast<const BYTE*>(data.c_str()),
                        static_cast<DWORD>((data.length() + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);
    return rc;
}

// Registers HKCR\CLSID\{clsid}\InprocServer32 pointing at this DLL.
static HRESULT RegisterInprocServer()
{
    std::wstring clsid = GuidToString(CLSID_MinimalPinyinIME);
    std::wstring base  = L"CLSID\\" + clsid;

    // Friendly name on the CLSID key.
    LONG rc = SetRegKeyValue(HKEY_CLASSES_ROOT, base, nullptr, TEXTSERVICE_DESC);
    if (rc != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(rc);

    // Module path.
    wchar_t module[MAX_PATH] = { 0 };
    if (GetModuleFileNameW(g_hInst, module, MAX_PATH) == 0)
        return HRESULT_FROM_WIN32(GetLastError());

    std::wstring inproc = base + L"\\InprocServer32";
    rc = SetRegKeyValue(HKEY_CLASSES_ROOT, inproc, nullptr, module);
    if (rc != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(rc);
    rc = SetRegKeyValue(HKEY_CLASSES_ROOT, inproc, L"ThreadingModel", L"Apartment");
    if (rc != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(rc);

    return S_OK;
}

static void UnregisterInprocServer()
{
    std::wstring clsid = GuidToString(CLSID_MinimalPinyinIME);
    std::wstring base  = L"CLSID\\" + clsid;
    // Delete the whole subtree (InprocServer32 then the CLSID key).
    RegDeleteKeyW(HKEY_CLASSES_ROOT, (base + L"\\InprocServer32").c_str());
    RegDeleteKeyW(HKEY_CLASSES_ROOT, base.c_str());
}

// Registers the TSF profile (language profile) for our text service.
static HRESULT RegisterProfile()
{
    ITfInputProcessorProfiles* pProfiles = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                                  CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfiles,
                                  reinterpret_cast<void**>(&pProfiles));
    if (FAILED(hr))
        return hr;

    hr = pProfiles->Register(CLSID_MinimalPinyinIME);
    RegLog(L"  ITfInputProcessorProfiles::Register hr=0x%08X", hr);
    if (SUCCEEDED(hr))
    {
        wchar_t module[MAX_PATH] = { 0 };
        GetModuleFileNameW(g_hInst, module, MAX_PATH);
        RegLog(L"  module path = %s", module);
        RegLog(L"  LANGID = 0x%04X, desc = %s", (unsigned)TEXTSERVICE_LANGID, TEXTSERVICE_DESC);

        hr = pProfiles->AddLanguageProfile(
            CLSID_MinimalPinyinIME,
            TEXTSERVICE_LANGID,
            GUID_Profile,
            TEXTSERVICE_DESC,
            static_cast<ULONG>(wcslen(TEXTSERVICE_DESC)),
            module,                        // icon file (the DLL)
            static_cast<ULONG>(wcslen(module)),
            TEXTSERVICE_ICON_INDEX);
        RegLog(L"  ITfInputProcessorProfiles::AddLanguageProfile hr=0x%08X", hr);
    }

    pProfiles->Release();
    return hr;
}

static void UnregisterProfile()
{
    ITfInputProcessorProfiles* pProfiles = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                                   CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfiles,
                                   reinterpret_cast<void**>(&pProfiles))))
    {
        pProfiles->Unregister(CLSID_MinimalPinyinIME);
        pProfiles->Release();
    }
}

// Registers the TSF categories that mark this CLSID's capabilities.
static HRESULT RegisterCategories()
{
    ITfCategoryMgr* pCategoryMgr = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr,
                                  CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr,
                                  reinterpret_cast<void**>(&pCategoryMgr));
    if (FAILED(hr))
        return hr;

    // Keyboard TIP category (marks us as a keyboard-style input processor).
    hr = pCategoryMgr->RegisterCategory(CLSID_MinimalPinyinIME,
                                        GUID_TFCAT_TIP_KEYBOARD,
                                        CLSID_MinimalPinyinIME);
    RegLog(L"  RegisterCategory(TIP_KEYBOARD) hr=0x%08X", hr);

    // Windows 8+/10/11: declare immersive (UWP / packaged app) support so the
    // IME is usable in modern apps.
    if (SUCCEEDED(hr))
    {
        hr = pCategoryMgr->RegisterCategory(CLSID_MinimalPinyinIME,
                                            GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,
                                            CLSID_MinimalPinyinIME);
        RegLog(L"  RegisterCategory(IMMERSIVESUPPORT) hr=0x%08X", hr);
    }

    // Declare systray support so the IME appears in the input indicator.
    if (SUCCEEDED(hr))
    {
        hr = pCategoryMgr->RegisterCategory(CLSID_MinimalPinyinIME,
                                            GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT,
                                            CLSID_MinimalPinyinIME);
        RegLog(L"  RegisterCategory(SYSTRAYSUPPORT) hr=0x%08X", hr);
    }

    pCategoryMgr->Release();
    return hr;
}

static void UnregisterCategories()
{
    ITfCategoryMgr* pCategoryMgr = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr,
                                   CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr,
                                   reinterpret_cast<void**>(&pCategoryMgr))))
    {
        pCategoryMgr->UnregisterCategory(CLSID_MinimalPinyinIME,
                                         GUID_TFCAT_TIP_KEYBOARD, CLSID_MinimalPinyinIME);
        pCategoryMgr->UnregisterCategory(CLSID_MinimalPinyinIME,
                                         GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT, CLSID_MinimalPinyinIME);
        pCategoryMgr->UnregisterCategory(CLSID_MinimalPinyinIME,
                                         GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT, CLSID_MinimalPinyinIME);
        pCategoryMgr->Release();
    }
}

// ---------------------------------------------------------------------------
// DllRegisterServer / DllUnregisterServer
// ---------------------------------------------------------------------------

STDAPI DllRegisterServer()
{
    // We need COM up to talk to the TSF managers.
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool coInited = SUCCEEDED(hrCo);

    RegLog(L"---- DllRegisterServer begin (elevated=%s, CoInit hr=0x%08X) ----",
           IsProcessElevated() ? L"yes" : L"NO", hrCo);

    HRESULT hr = RegisterInprocServer();
    RegLog(L"RegisterInprocServer hr=0x%08X", hr);
    if (SUCCEEDED(hr))
    {
        hr = RegisterProfile();
        RegLog(L"RegisterProfile overall hr=0x%08X", hr);
    }
    if (SUCCEEDED(hr))
    {
        hr = RegisterCategories();
        RegLog(L"RegisterCategories overall hr=0x%08X", hr);
    }

    if (FAILED(hr))
    {
        // Roll back partial registration.
        UnregisterCategories();
        UnregisterProfile();
        UnregisterInprocServer();
    }

    if (coInited)
        CoUninitialize();

    RegLog(L"---- DllRegisterServer end hr=0x%08X ----", hr);

    // Return the actual failing HRESULT (not a blanket SELFREG_E_CLASS) so the
    // real cause is visible to regsvr32 / the CI registration smoke test.
    return hr;
}

STDAPI DllUnregisterServer()
{
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool coInited = SUCCEEDED(hrCo);

    UnregisterCategories();
    UnregisterProfile();
    UnregisterInprocServer();

    if (coInited)
        CoUninitialize();

    return S_OK;
}
