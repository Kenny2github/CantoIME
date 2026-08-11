// Settings.cpp

#include "Settings.h"
#include "Globals.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cwctype>
#include <algorithm>

// HKCU value names under CANTO_REG_SUBKEY (Software\CantoIME).
static const wchar_t* kValDictSrc  = L"Dictionary";      // traditional|simplified|custom
static const wchar_t* kValDictPath = L"DictionaryPath";  // used only for custom
static const wchar_t* kValFontSize = L"CandidateFontSize";
static const wchar_t* kValToggle   = L"ToggleKey";       // 0=none 1=L 2=R 3=both

// Subkey holding user candidate overrides: one REG_SZ value per key, whose data
// is the space-separated candidate list.
static const wchar_t* kOverridesBase = L"Software\\CantoIME\\Overrides";

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

// --- User candidate overrides ------------------------------------------------

static std::wstring LowerTrim(const std::wstring& s)
{
    size_t a = 0, b = s.size();
    while (a < b && iswspace(s[a])) ++a;
    while (b > a && iswspace(s[b - 1])) --b;
    std::wstring out = s.substr(a, b - a);
    for (auto& c : out)
        c = static_cast<wchar_t>(towlower(c));
    return out;
}

// Splits on runs of whitespace, dropping empty tokens.
static std::vector<std::wstring> SplitTokens(const std::wstring& s)
{
    std::vector<std::wstring> out;
    size_t i = 0;
    while (i < s.size())
    {
        while (i < s.size() && iswspace(s[i])) ++i;
        size_t start = i;
        while (i < s.size() && !iswspace(s[i])) ++i;
        if (i > start)
            out.push_back(s.substr(start, i - start));
    }
    return out;
}

// FNV-1a 64-bit over the UTF-16 code units, as 16 lowercase hex digits. Keeps a
// custom dictionary's override subkey name registry-safe (paths contain '\\').
static std::wstring HashPath(const std::wstring& p)
{
    unsigned long long h = 1469598103934665603ULL;
    for (wchar_t c : p)
    {
        h ^= static_cast<unsigned long long>(static_cast<unsigned short>(c));
        h *= 1099511628211ULL;
    }
    const wchar_t* hex = L"0123456789abcdef";
    std::wstring out(16, L'0');
    for (int i = 15; i >= 0; --i) { out[i] = hex[h & 0xF]; h >>= 4; }
    return out;
}

std::wstring DictionaryIdentity(DictSource src, const std::wstring& customPath)
{
    switch (src)
    {
    case DictSource::StockSimplified: return L"simp";
    case DictSource::Custom:
    {
        std::wstring p = LowerTrim(customPath);
        if (p.empty()) return std::wstring();
        return L"custom-" + HashPath(p);
    }
    default: return L"trad";
    }
}

static std::wstring OverridesSubkey(const std::wstring& dictId)
{
    return std::wstring(kOverridesBase) + L"\\" + dictId;
}

std::vector<CantoOverride> LoadOverrides(const std::wstring& dictId)
{
    std::vector<CantoOverride> out;
    if (dictId.empty())
        return out;

    std::wstring subkey = OverridesSubkey(dictId);
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0,
                      KEY_READ | KEY_WOW64_64KEY, &hKey) != ERROR_SUCCESS)
        return out;

    for (DWORD i = 0; ; ++i)
    {
        wchar_t name[256];
        DWORD nameLen = 256;
        DWORD type = 0, cb = 0;
        LONG r = RegEnumValueW(hKey, i, name, &nameLen, nullptr, &type, nullptr, &cb);
        if (r == ERROR_NO_MORE_ITEMS)
            break;
        if (r != ERROR_SUCCESS || type != REG_SZ || cb == 0)
            continue;

        std::wstring data(cb / sizeof(wchar_t), L'\0');
        DWORD cb2 = cb;
        if (RegQueryValueExW(hKey, name, nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(&data[0]), &cb2) != ERROR_SUCCESS)
            continue;
        size_t nul = data.find(L'\0');
        if (nul != std::wstring::npos)
            data.resize(nul);

        std::wstring key = LowerTrim(name);
        std::vector<std::wstring> cands = SplitTokens(data);
        if (!key.empty() && !cands.empty())
            out.emplace_back(key, std::move(cands));
    }
    RegCloseKey(hKey);

    std::sort(out.begin(), out.end(),
              [](const CantoOverride& a, const CantoOverride& b) { return a.first < b.first; });
    return out;
}

bool SaveOverrides(const std::wstring& dictId,
                   const std::vector<CantoOverride>& overrides)
{
    if (dictId.empty())
        return true; // no dictionary to scope to (e.g. Custom with no path yet)

    std::wstring subkey = OverridesSubkey(dictId);

    // Rewrite the whole subkey so removed entries actually disappear.
    RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str());

    if (overrides.empty())
        return true; // nothing to write; the cleared subkey is the desired state

    HKEY hKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE | KEY_WOW64_64KEY,
                        nullptr, &hKey, nullptr) != ERROR_SUCCESS)
        return false;

    bool ok = true;
    for (const CantoOverride& ov : overrides)
    {
        std::wstring key = LowerTrim(ov.first);
        if (key.empty() || ov.second.empty())
            continue;

        std::wstring data;
        for (size_t i = 0; i < ov.second.size(); ++i)
        {
            if (i) data += L' ';
            data += ov.second[i];
        }

        ok &= RegSetValueExW(hKey, key.c_str(), 0, REG_SZ,
                             reinterpret_cast<const BYTE*>(data.c_str()),
                             static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t)))
              == ERROR_SUCCESS;
    }

    RegCloseKey(hKey);
    return ok;
}
