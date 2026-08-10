// Settings.cpp

#include "Settings.h"
#include "Globals.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// HKCU value names under CANTO_REG_SUBKEY (Software\CantoIME).
static const wchar_t* kValDictSrc  = L"Dictionary";      // traditional|simplified|custom
static const wchar_t* kValDictPath = L"DictionaryPath";  // used only for custom
static const wchar_t* kValFontSize = L"CandidateFontSize";
static const wchar_t* kValToggle   = L"ToggleKey";       // 0=none 1=L 2=R 3=both

static const int kMinFont = 8;
static const int kMaxFont = 72;

static int ClampFont(int v)
{
    if (v < kMinFont) return kMinFont;
    if (v > kMaxFont) return kMaxFont;
    return v;
}

static bool ReadString(HKEY hKey, const wchar_t* name, std::wstring& out)
{
    DWORD type = 0, cb = 0;
    if (RegQueryValueExW(hKey, name, nullptr, &type, nullptr, &cb) != ERROR_SUCCESS ||
        type != REG_SZ || cb == 0)
        return false;

    std::wstring buf(cb / sizeof(wchar_t), L'\0');
    if (RegQueryValueExW(hKey, name, nullptr, nullptr,
                         reinterpret_cast<LPBYTE>(&buf[0]), &cb) != ERROR_SUCCESS)
        return false;

    size_t nul = buf.find(L'\0');
    if (nul != std::wstring::npos)
        buf.resize(nul);
    out = buf;
    return true;
}

static bool ReadDword(HKEY hKey, const wchar_t* name, DWORD& out)
{
    DWORD type = 0, cb = sizeof(DWORD), val = 0;
    if (RegQueryValueExW(hKey, name, nullptr, &type,
                         reinterpret_cast<LPBYTE>(&val), &cb) != ERROR_SUCCESS ||
        type != REG_DWORD)
        return false;
    out = val;
    return true;
}

CantoSettings LoadSettings()
{
    CantoSettings s;
    s.fontSize = CANTO_DEFAULT_FONT_SIZE;
    // Default stock follows the variant's script until the user chooses.
    s.dictSource = CANTO_IS_SIMPLIFIED ? DictSource::StockSimplified
                                       : DictSource::StockTraditional;

    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, CANTO_REG_SUBKEY, 0,
                      KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS)
    {
        std::wstring src;
        if (ReadString(hKey, kValDictSrc, src))
        {
            if (src == L"custom")           s.dictSource = DictSource::Custom;
            else if (src == L"simplified")  s.dictSource = DictSource::StockSimplified;
            else if (src == L"traditional") s.dictSource = DictSource::StockTraditional;
        }
        ReadString(hKey, kValDictPath, s.dictionaryPath);

        DWORD dw = 0;
        if (ReadDword(hKey, kValFontSize, dw))
            s.fontSize = ClampFont(static_cast<int>(dw));
        if (ReadDword(hKey, kValToggle, dw) && dw <= 3)
            s.toggleKey = static_cast<ToggleKeyMode>(dw);

        RegCloseKey(hKey);
    }

    s.fontSize = ClampFont(s.fontSize);
    return s;
}

bool SaveSettings(const CantoSettings& s)
{
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, CANTO_REG_SUBKEY, 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE | KEY_WOW64_64KEY,
                        nullptr, &hKey, nullptr) != ERROR_SUCCESS)
        return false;

    bool ok = true;

    const wchar_t* src = L"traditional";
    if (s.dictSource == DictSource::Custom)               src = L"custom";
    else if (s.dictSource == DictSource::StockSimplified) src = L"simplified";
    ok &= RegSetValueExW(hKey, kValDictSrc, 0, REG_SZ,
                         reinterpret_cast<const BYTE*>(src),
                         static_cast<DWORD>((wcslen(src) + 1) * sizeof(wchar_t)))
          == ERROR_SUCCESS;

    ok &= RegSetValueExW(hKey, kValDictPath, 0, REG_SZ,
                         reinterpret_cast<const BYTE*>(s.dictionaryPath.c_str()),
                         static_cast<DWORD>((s.dictionaryPath.size() + 1) * sizeof(wchar_t)))
          == ERROR_SUCCESS;

    DWORD font = static_cast<DWORD>(ClampFont(s.fontSize));
    ok &= RegSetValueExW(hKey, kValFontSize, 0, REG_DWORD,
                         reinterpret_cast<const BYTE*>(&font), sizeof(font))
          == ERROR_SUCCESS;

    DWORD toggle = static_cast<DWORD>(s.toggleKey);
    ok &= RegSetValueExW(hKey, kValToggle, 0, REG_DWORD,
                         reinterpret_cast<const BYTE*>(&toggle), sizeof(toggle))
          == ERROR_SUCCESS;

    RegCloseKey(hKey);
    return ok;
}
