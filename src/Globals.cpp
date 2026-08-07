// Globals.cpp
//
// Definitions of the shared globals declared in Globals.h.

#include "Globals.h"
#include <shlobj.h>   // SHGetFolderPath for %APPDATA%
#include <string>

// {A3D4E1F2-5B6C-4D7E-8F90-1A2B3C4D5E6F}
const CLSID CLSID_MinimalPinyinIME =
    { 0xA3D4E1F2, 0x5B6C, 0x4D7E, { 0x8F, 0x90, 0x1A, 0x2B, 0x3C, 0x4D, 0x5E, 0x6F } };

// {B4E5F2A3-6C7D-4E8F-9A01-2B3C4D5E6F70}
const GUID GUID_Profile =
    { 0xB4E5F2A3, 0x6C7D, 0x4E8F, { 0x9A, 0x01, 0x2B, 0x3C, 0x4D, 0x5E, 0x6F, 0x70 } };

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
    if (ReadRegString(HKEY_CURRENT_USER, MPIME_REG_SUBKEY, MPIME_REG_DICTVALUE, path) &&
        FileExists(path))
        return path;
    if (ReadRegString(HKEY_LOCAL_MACHINE, MPIME_REG_SUBKEY, MPIME_REG_DICTVALUE, path) &&
        FileExists(path))
        return path;

    // (2) dictionary.txt next to the DLL.
    std::wstring dir = GetModuleDirectory();
    if (!dir.empty())
    {
        std::wstring candidate = dir + MPIME_DICT_FILENAME;
        if (FileExists(candidate))
            return candidate;
    }

    // (3) %APPDATA%\MinimalPinyinIME\dictionary.txt
    wchar_t appdata[MAX_PATH] = { 0 };
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appdata)))
    {
        std::wstring candidate = std::wstring(appdata) + L"\\MinimalPinyinIME\\" + MPIME_DICT_FILENAME;
        if (FileExists(candidate))
            return candidate;
    }

    // Nothing found: return the DLL-adjacent path as a best-effort default so
    // the caller has a stable place to look. Dictionary load will simply yield
    // no candidates if the file is missing.
    if (!dir.empty())
        return dir + MPIME_DICT_FILENAME;
    return std::wstring();
}
