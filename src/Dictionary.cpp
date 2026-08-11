// Dictionary.cpp

#include "Dictionary.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cwctype>
#include <algorithm>
#include <unordered_set>

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

        // Read the romanization key (up to first whitespace run).
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

        // Drop repeated characters within a key, preserving first-seen order.
        // Several dictionary lines list their whole candidate set twice, which
        // would otherwise surface every character as a duplicate candidate.
        if (cands.size() > 1)
        {
            std::unordered_set<std::wstring> seen;
            std::vector<std::wstring> unique;
            unique.reserve(cands.size());
            for (auto& c : cands)
                if (seen.insert(c).second)
                    unique.push_back(std::move(c));
            cands = std::move(unique);
        }

        if (!cands.empty())
        {
            if (key.size() > m_maxKeyLen)
                m_maxKeyLen = key.size();
            m_map[key] = std::move(cands); // later duplicate keys override earlier
        }
    }

    // Build the sorted key list used for prefix scans. Done once per load so
    // KeysWithPrefix can binary-search instead of scanning the whole map.
    RebuildIndex();

    return true;
}

void Dictionary::RebuildIndex()
{
    m_sortedKeys.clear();
    m_sortedKeys.reserve(m_map.size());
    for (const auto& kv : m_map)
        m_sortedKeys.push_back(kv.first);
    std::sort(m_sortedKeys.begin(), m_sortedKeys.end());
}

void Dictionary::ApplyOverrides(
    const std::vector<std::pair<std::wstring, std::vector<std::wstring>>>& overrides)
{
    if (overrides.empty())
        return;

    bool changed = false;
    for (const auto& ov : overrides)
    {
        std::wstring key = ToLower(ov.first);
        if (key.empty() || ov.second.empty())
            continue;

        // Prepend before the stock candidates so the user's extras take the
        // low positions (the first is pickable with the 1 key). Dedup: an
        // override that duplicates a stock character surfaces only at the
        // front, not twice.
        std::vector<std::wstring>& vec = m_map[key];
        std::vector<std::wstring> merged;
        std::unordered_set<std::wstring> seen;
        for (const std::wstring& c : ov.second)
            if (seen.insert(c).second)
                merged.push_back(c);
        for (const std::wstring& c : vec)
            if (seen.insert(c).second)
                merged.push_back(c);
        vec.swap(merged);

        if (key.size() > m_maxKeyLen)
            m_maxKeyLen = key.size();
        changed = true;
    }

    if (changed)
        RebuildIndex();
}

bool Dictionary::Reload()
{
    if (m_path.empty())
        return false;
    return Load(m_path);
}

const std::vector<std::wstring>& Dictionary::Lookup(const std::wstring& romanization) const
{
    std::wstring key = ToLower(romanization);
    auto it = m_map.find(key);
    if (it == m_map.end())
        return m_empty;
    return it->second;
}

bool Dictionary::HasExact(const std::wstring& romanization) const
{
    return m_map.find(ToLower(romanization)) != m_map.end();
}

std::vector<std::wstring> Dictionary::KeysWithPrefix(const std::wstring& prefix,
                                                     size_t maxKeys) const
{
    std::vector<std::wstring> out;
    if (prefix.empty() || maxKeys == 0)
        return out;

    std::wstring lo = ToLower(prefix);

    // First key >= prefix; every key sharing the prefix is contiguous from here.
    auto it = std::lower_bound(m_sortedKeys.begin(), m_sortedKeys.end(), lo);
    for (; it != m_sortedKeys.end() && out.size() < maxKeys; ++it)
    {
        // Stop as soon as a key no longer starts with the prefix.
        if (it->compare(0, lo.size(), lo) != 0)
            break;
        out.push_back(*it);
    }
    return out;
}

size_t Dictionary::LongestExactPrefixLen(const std::wstring& s, size_t start) const
{
    if (start >= s.size())
        return 0;

    size_t maxLen = s.size() - start;
    if (m_maxKeyLen && maxLen > m_maxKeyLen)
        maxLen = m_maxKeyLen;

    // Longest wins: jyutping keys are short, so a top-down probe is cheap.
    for (size_t len = maxLen; len >= 1; --len)
    {
        if (m_map.find(s.substr(start, len)) != m_map.end())
            return len;
    }
    return 0;
}
