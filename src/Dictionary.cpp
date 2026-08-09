// Dictionary.cpp

#include "Dictionary.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cwctype>

// Converts a UTF-8 byte buffer to a wide (UTF-16) string.
static std::wstring Utf8ToWide(const char* data, int len)
{
    if (len <= 0)
        return std::wstring();
    int wlen = MultiByteToWideChar(CP_UTF8, 0, data, len, nullptr, 0);
    if (wlen <= 0)
        return std::wstring();
    std::wstring w(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, data, len, &w[0], wlen);
    return w;
}

static std::wstring ToLower(const std::wstring& s)
{
    std::wstring out = s;
    for (auto& c : out)
        c = static_cast<wchar_t>(towlower(c));
    return out;
}

static bool IsSpace(wchar_t c)
{
    return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
}

bool Dictionary::Load(const std::wstring& path)
{
    m_path = path;
    m_map.clear();

    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    LARGE_INTEGER size;
    if (!GetFileSizeEx(hFile, &size) || size.QuadPart <= 0 || size.QuadPart > (64 * 1024 * 1024))
    {
        CloseHandle(hFile);
        return false;
    }

    DWORD toRead = static_cast<DWORD>(size.QuadPart);
    std::vector<char> bytes(toRead);
    DWORD read = 0;
    BOOL ok = ReadFile(hFile, bytes.data(), toRead, &read, nullptr);
    CloseHandle(hFile);
    if (!ok)
        return false;

    // Skip a UTF-8 BOM if present.
    const char* start = bytes.data();
    int count = static_cast<int>(read);
    if (count >= 3 && (unsigned char)start[0] == 0xEF &&
        (unsigned char)start[1] == 0xBB && (unsigned char)start[2] == 0xBF)
    {
        start += 3;
        count -= 3;
    }

    std::wstring text = Utf8ToWide(start, count);

    // Parse line by line.
    size_t pos = 0;
    while (pos < text.size())
    {
        size_t eol = text.find(L'\n', pos);
        std::wstring line = (eol == std::wstring::npos)
                                ? text.substr(pos)
                                : text.substr(pos, eol - pos);
        pos = (eol == std::wstring::npos) ? text.size() : eol + 1;

        // Trim leading whitespace.
        size_t s = 0;
        while (s < line.size() && IsSpace(line[s]))
            ++s;
        if (s >= line.size())
            continue;
        if (line[s] == L'#')
            continue; // comment

        // Read the pinyin key (up to first whitespace run).
        size_t keyStart = s;
        while (s < line.size() && !IsSpace(line[s]))
            ++s;
        std::wstring key = ToLower(line.substr(keyStart, s - keyStart));
        if (key.empty())
            continue;

        // Read the remaining tokens as candidates (whitespace-separated).
        std::vector<std::wstring> cands;
        while (s < line.size())
        {
            while (s < line.size() && IsSpace(line[s]))
                ++s;
            size_t tokStart = s;
            while (s < line.size() && !IsSpace(line[s]))
                ++s;
            if (s > tokStart)
                cands.push_back(line.substr(tokStart, s - tokStart));
        }

        if (!cands.empty())
            m_map[key] = std::move(cands); // later duplicate keys override earlier
    }

    return true;
}

bool Dictionary::Reload()
{
    if (m_path.empty())
        return false;
    return Load(m_path);
}

const std::vector<std::wstring>& Dictionary::Lookup(const std::wstring& pinyin) const
{
    std::wstring key = ToLower(pinyin);
    auto it = m_map.find(key);
    if (it == m_map.end())
        return m_empty;
    return it->second;
}
