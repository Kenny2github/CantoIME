// dllmain.cpp
//
// DLL entry point, COM class factory, and self-registration for the CantoIME
// text service.
//
// Registration performs three things:
//   1. Register the in-proc COM server (HKCR\CLSID\{clsid}\InprocServer32).
//   2. Register the TSF text service + language profile for this variant's
//      LANGID (TEXTSERVICE_LANGID: HK 0x0C04, TW 0x0404, or MO 0x1404).
//   3. Register TSF categories, including the Windows-11-critical
//      IMMERSIVESUPPORT and SYSTRAYSUPPORT capabilities so the IME works in
//      modern (UWP / packaged / store) apps and the systray shows it.

#include "Globals.h"
#include "TextService.h"

#include <msctf.h>
#include <olectl.h>
#include <new>
#include <string>

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

    if (!IsEqualCLSID(rclsid, CLSID_CantoIME))
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
    std::wstring clsid = GuidToString(CLSID_CantoIME);
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
    std::wstring clsid = GuidToString(CLSID_CantoIME);
    std::wstring base  = L"CLSID\\" + clsid;
    // Delete the whole subtree (InprocServer32 then the CLSID key).
    RegDeleteKeyW(HKEY_CLASSES_ROOT, (base + L"\\InprocServer32").c_str());
    RegDeleteKeyW(HKEY_CLASSES_ROOT, base.c_str());
}

// Registers the TSF text service + its language profile.
//
// We use the modern ITfInputProcessorProfileMgr::RegisterProfile (Vista+), NOT
// the legacy ITfInputProcessorProfiles::Register + AddLanguageProfile, because:
//   * the profile is bound to TEXTSERVICE_LANGID (no phantom entry under the
//     current input language);
//   * bEnabledByDefault = FALSE means the IME is NOT auto-added to the input
//     list. It shows under "Add a keyboard" for the language and obeys the
//     installed-keyboards list (add/remove sticks) instead of clinging to the
//     Win+Space menu. dwFlags = 0 so it is visible in the settings UI.
// The icon is index 0 = the DLL's icon resource (our 粵 glyph, a generated .res).
static HRESULT RegisterProfile()
{
    ITfInputProcessorProfileMgr* pMgr = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                                  CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfileMgr,
                                  reinterpret_cast<void**>(&pMgr));
    if (FAILED(hr))
        return hr;

    wchar_t module[MAX_PATH] = { 0 };
    DWORD n = GetModuleFileNameW(g_hInst, module, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
    {
        pMgr->Release();
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }

    hr = pMgr->RegisterProfile(
        CLSID_CantoIME,
        TEXTSERVICE_LANGID,
        GUID_Profile,
        TEXTSERVICE_DESC,
        static_cast<ULONG>(wcslen(TEXTSERVICE_DESC)),
        module,                     // icon file (this DLL)
        n,                          // length of the module path, in chars
        TEXTSERVICE_ICON_INDEX,     // 0 -> the DLL's icon resource
        nullptr,                    // no substitute keyboard layout
        0,                          // no preferred layout
        FALSE,                      // NOT enabled by default (obey add/remove)
        0);                         // dwFlags: 0 -> visible in the settings UI

    pMgr->Release();
    return hr;
}

static void UnregisterProfile()
{
    ITfInputProcessorProfileMgr* pMgr = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                                   CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfileMgr,
                                   reinterpret_cast<void**>(&pMgr))))
    {
        pMgr->UnregisterProfile(CLSID_CantoIME, TEXTSERVICE_LANGID, GUID_Profile, 0);
        pMgr->Release();
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
    hr = pCategoryMgr->RegisterCategory(CLSID_CantoIME,
                                        GUID_TFCAT_TIP_KEYBOARD,
                                        CLSID_CantoIME);

    // Windows 8+/10/11: declare immersive (UWP / packaged app) support so the
    // IME is usable in modern apps.
    if (SUCCEEDED(hr))
        hr = pCategoryMgr->RegisterCategory(CLSID_CantoIME,
                                            GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,
                                            CLSID_CantoIME);

    // Declare systray support so the IME appears in the input indicator.
    if (SUCCEEDED(hr))
        hr = pCategoryMgr->RegisterCategory(CLSID_CantoIME,
                                            GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT,
                                            CLSID_CantoIME);

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
        pCategoryMgr->UnregisterCategory(CLSID_CantoIME,
                                         GUID_TFCAT_TIP_KEYBOARD, CLSID_CantoIME);
        pCategoryMgr->UnregisterCategory(CLSID_CantoIME,
                                         GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT, CLSID_CantoIME);
        pCategoryMgr->UnregisterCategory(CLSID_CantoIME,
                                         GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT, CLSID_CantoIME);
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

    HRESULT hr = RegisterInprocServer();
    if (FAILED(hr))
        OutputDebugStringW(L"[CantoIME] RegisterInprocServer failed\n");
    if (SUCCEEDED(hr))
    {
        hr = RegisterProfile();
        if (FAILED(hr))
            OutputDebugStringW(L"[CantoIME] RegisterProfile failed\n");
    }
    if (SUCCEEDED(hr))
    {
        hr = RegisterCategories();
        if (FAILED(hr))
            OutputDebugStringW(L"[CantoIME] RegisterCategories failed\n");
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
