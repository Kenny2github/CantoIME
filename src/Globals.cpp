// Globals.cpp
//
// Definitions of the shared globals declared in Globals.h.

#include "Globals.h"
#include "Settings.h"
#include <string>

// Per-variant COM identity. Each Traditional Chinese variant is a separate
// COM server and language profile, so each needs its own CLSID + profile GUID.
#if defined(CANTO_VARIANT_HK)
// {BC8FB43B-879A-4B9C-8723-3FE7556D57E2}
const CLSID CLSID_CantoIME =
    { 0xBC8FB43B, 0x879A, 0x4B9C, { 0x87, 0x23, 0x3F, 0xE7, 0x55, 0x6D, 0x57, 0xE2 } };
// {38F83831-D93F-473A-97B7-8E590D41A72D}
const GUID GUID_Profile =
    { 0x38F83831, 0xD93F, 0x473A, { 0x97, 0xB7, 0x8E, 0x59, 0x0D, 0x41, 0xA7, 0x2D } };
#elif defined(CANTO_VARIANT_TW)
// {C732EC0C-99F6-43EC-83C5-42EC1C944325}
const CLSID CLSID_CantoIME =
    { 0xC732EC0C, 0x99F6, 0x43EC, { 0x83, 0xC5, 0x42, 0xEC, 0x1C, 0x94, 0x43, 0x25 } };
// {973BC8C9-A5A2-4BC6-90AB-8CF4EA25FFBF}
const GUID GUID_Profile =
    { 0x973BC8C9, 0xA5A2, 0x4BC6, { 0x90, 0xAB, 0x8C, 0xF4, 0xEA, 0x25, 0xFF, 0xBF } };
#elif defined(CANTO_VARIANT_MO)
// {7E96AB83-F0BF-4F24-B41F-2E32BD562DA6}
const CLSID CLSID_CantoIME =
    { 0x7E96AB83, 0xF0BF, 0x4F24, { 0xB4, 0x1F, 0x2E, 0x32, 0xBD, 0x56, 0x2D, 0xA6 } };
// {2CB45F66-00A5-418C-8354-87AC7B7C8C85}
const GUID GUID_Profile =
    { 0x2CB45F66, 0x00A5, 0x418C, { 0x83, 0x54, 0x87, 0xAC, 0x7B, 0x7C, 0x8C, 0x85 } };
#elif defined(CANTO_VARIANT_CN)
// {7C5C8388-430C-4C15-8A87-78A0BB9A482B}
const CLSID CLSID_CantoIME =
    { 0x7C5C8388, 0x430C, 0x4C15, { 0x8A, 0x87, 0x78, 0xA0, 0xBB, 0x9A, 0x48, 0x2B } };
// {1307330C-CDBA-484F-891F-FEB4595D437D}
const GUID GUID_Profile =
    { 0x1307330C, 0xCDBA, 0x484F, { 0x89, 0x1F, 0xFE, 0xB4, 0x59, 0x5D, 0x43, 0x7D } };
#elif defined(CANTO_VARIANT_SG)
// {B551180B-E874-4B04-8B8F-3F16344AD683}
const CLSID CLSID_CantoIME =
    { 0xB551180B, 0xE874, 0x4B04, { 0x8B, 0x8F, 0x3F, 0x16, 0x34, 0x4A, 0xD6, 0x83 } };
// {43CCFF03-2E82-4938-88CB-15D4CBE069A7}
const GUID GUID_Profile =
    { 0x43CCFF03, 0x2E82, 0x4938, { 0x88, 0xCB, 0x15, 0xD4, 0xCB, 0xE0, 0x69, 0xA7 } };
#endif

// Shared across variants (the language-bar item is keyed by the per-variant
// clsidService as well).
// {6252B049-21DE-4270-B3F9-70CA49BF7AFE}
const GUID GUID_CantoMainButton =
    { 0x6252B049, 0x21DE, 0x4270, { 0xB3, 0xF9, 0x70, 0xCA, 0x49, 0xBF, 0x7A, 0xFE } };
// {7B2AFF87-43E6-4F42-8430-0D60D97F6BB9}
const GUID GUID_CantoSettingsButton =
    { 0x7B2AFF87, 0x43E6, 0x4F42, { 0x84, 0x30, 0x0D, 0x60, 0xD9, 0x7F, 0x6B, 0xB9 } };

HINSTANCE g_hInst = nullptr;
LONG      g_cRefDll = 0;

void DllAddRef()
{
    InterlockedIncrement(&g_cRefDll);
}

void DllRelease()
{
    InterlockedDecrement(&g_cRefDll);
}

std::wstring GetModuleDirectory()
{
    wchar_t path[MAX_PATH] = { 0 };
    DWORD n = GetModuleFileNameW(g_hInst, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return std::wstring();

    // Strip the file name, keep the trailing backslash.
    std::wstring dir(path);
    size_t slash = dir.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
        return std::wstring();
    return dir.substr(0, slash + 1);
}

// Expands environment variables (e.g. %APPDATA%) in a path. Returns the input
// unchanged if expansion fails or would overflow.
static std::wstring ExpandEnv(const std::wstring& s)
{
    wchar_t buf[MAX_PATH] = { 0 };
    DWORD n = ExpandEnvironmentStringsW(s.c_str(), buf, MAX_PATH);
    if (n == 0 || n > MAX_PATH)
        return s;
    return std::wstring(buf);
}

std::wstring ResolveDictionaryPath()
{
    // The Dictionary setting is the sole selector (no registry search, no
    // %APPDATA%). Stock choices map to the bundled file next to the DLL; Custom
    // maps to the configured path. Load yields no candidates if it is missing.
    CantoSettings settings = LoadSettings();

    if (settings.dictSource == DictSource::Custom)
        return ExpandEnv(settings.dictionaryPath);

    std::wstring dir = GetModuleDirectory();
    if (dir.empty())
        return std::wstring();

    return dir + (settings.dictSource == DictSource::StockSimplified
                      ? CANTO_DICT_SIMPLIFIED
                      : CANTO_DICT_FILENAME);
}
