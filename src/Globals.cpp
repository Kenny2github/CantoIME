// Globals.cpp
//
// Definitions of the shared globals declared in Globals.h.

#include "Globals.h"
#include <shlobj.h>   // SHGetFolderPath for %APPDATA%
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
#endif

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

// Reads a REG_SZ value from either HKCU or HKLM. Returns true on success.
static bool ReadRegString(HKEY root, const wchar_t* subkey, const wchar_t* value, std::wstring& out)
{
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ | KEY_WOW64_64KEY, &hKey) != ERROR_SUCCESS)
        return false;

    wchar_t buf[MAX_PATH] = { 0 };
    DWORD cb = sizeof(buf);
    DWORD type = 0;
    LONG rc = RegQueryValueExW(hKey, value, nullptr, &type,
                               reinterpret_cast<LPBYTE>(buf), &cb);
    RegCloseKey(hKey);

    if (rc != ERROR_SUCCESS || type != REG_SZ)
        return false;

    out.assign(buf);
    return !out.empty();
}

static bool FileExists(const std::wstring& path)
{
    if (path.empty())
        return false;
    DWORD attr = GetFileAttributesW(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES) && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring ResolveDictionaryPath()
{
    std::wstring path;

    // (1) Registry override: prefer HKCU, then HKLM.
    if (ReadRegString(HKEY_CURRENT_USER, CANTO_REG_SUBKEY, CANTO_REG_DICTVALUE, path) &&
        FileExists(path))
        return path;
    if (ReadRegString(HKEY_LOCAL_MACHINE, CANTO_REG_SUBKEY, CANTO_REG_DICTVALUE, path) &&
        FileExists(path))
        return path;

    // (2) dictionary.txt next to the DLL.
    std::wstring dir = GetModuleDirectory();
    if (!dir.empty())
    {
        std::wstring candidate = dir + CANTO_DICT_FILENAME;
        if (FileExists(candidate))
            return candidate;
    }

    // (3) %APPDATA%\CantoIME\dictionary.txt
    wchar_t appdata[MAX_PATH] = { 0 };
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appdata)))
    {
        std::wstring candidate = std::wstring(appdata) + L"\\CantoIME\\" + CANTO_DICT_FILENAME;
        if (FileExists(candidate))
            return candidate;
    }

    // Nothing found: return the DLL-adjacent path as a best-effort default so
    // the caller has a stable place to look. Dictionary load will simply yield
    // no candidates if the file is missing.
    if (!dir.empty())
        return dir + CANTO_DICT_FILENAME;
    return std::wstring();
}
