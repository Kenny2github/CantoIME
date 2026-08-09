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

// Reads a UTF-8 file into a wide string, skipping a leading BOM. Returns false
// if the file cannot be opened, is empty, or is implausibly large.
static bool ReadFileToWide(const std::wstring& path, std::wstring& out)
{
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

    const char* start = bytes.data();
    int count = static_cast<int>(read);
    if (count >= 3 && (unsigned char)start[0] == 0xEF &&
        (unsigned char)start[1] == 0xBB && (unsigned char)start[2] == 0xBF)
    {
        start += 3;
        count -= 3;
    }

    out = Utf8ToWide(start, count);
    return true;
}

// Drops repeated tokens, preserving first-seen order. Several dictionary lines
// list their whole candidate set twice, which would otherwise surface every
// character as a duplicate candidate.
static void DedupPreserveOrder(std::vector<std::wstring>& v)
{
    if (v.size() <= 1)
        return;
    std::unordered_set<std::wstring> seen;
    std::vector<std::wstring> unique;
    unique.reserve(v.size());
    for (auto& c : v)
        if (seen.insert(c).second)
            unique.push_back(std::move(c));
    v = std::move(unique);
}

// Splits "key char char ..." starting at `s` into a lowercased key and its
// whitespace-separated candidate tokens. Returns false if there is no key.
// Does not dedup (the caller decides, since prepend dedups across two lists).
static bool SplitEntry(const std::wstring& line, size_t s,
                       std::wstring& key, std::vector<std::wstring>& cands)
{
    while (s < line.size() && IsSpace(line[s]))
        ++s;
    size_t keyStart = s;
    while (s < line.size() && !IsSpace(line[s]))
        ++s;
    key = ToLower(line.substr(keyStart, s - keyStart));
    if (key.empty())
        return false;

    cands.clear();
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
    return true;
}

// Calls fn(line, firstNonSpace) for each non-blank, non-comment line of text.
template <typename Fn>
static void ForEachEntryLine(const std::wstring& text, Fn fn)
{
    size_t pos = 0;
    while (pos < text.size())
    {
        size_t eol = text.find(L'\n', pos);
        std::wstring line = (eol == std::wstring::npos)
                                ? text.substr(pos)
                                : text.substr(pos, eol - pos);
        pos = (eol == std::wstring::npos) ? text.size() : eol + 1;

        size_t s = 0;
        while (s < line.size() && IsSpace(line[s]))
            ++s;
        if (s >= line.size() || line[s] == L'#')
            continue;
        fn(line, s);
    }
}

bool Dictionary::Load(const std::wstring& path)
{
    m_path = path;
    m_overridePath.clear();
    m_map.clear();

    std::wstring text;
    if (!ReadFileToWide(path, text))
    {
        Rebuild();
        return false;
    }

    ForEachEntryLine(text, [this](const std::wstring& line, size_t s)
    {
        std::wstring key;
        std::vector<std::wstring> cands;
        if (!SplitEntry(line, s, key, cands))
            return;
        DedupPreserveOrder(cands);
        if (!cands.empty())
            m_map[key] = std::move(cands); // later duplicate keys override earlier
    });

    Rebuild();
    return true;
}

bool Dictionary::ApplyOverrides(const std::wstring& path)
{
    m_overridePath = path;

    std::wstring text;
    if (!ReadFileToWide(path, text))
        return false;

    ForEachEntryLine(text, [this](const std::wstring& line, size_t s)
    {
        wchar_t op = line[s];
        if (op != L'+' && op != L'=' && op != L'-')
            return; // override lines must carry an operator; ignore anything else

        std::wstring key;
        std::vector<std::wstring> cands;
        // The operator is one char; parse the remainder as a normal entry.
        // '-' takes only a key, but SplitEntry harmlessly ignores extra tokens.
        if (!SplitEntry(line, s + 1, key, cands))
            return;

        if (op == L'-')
        {
            m_map.erase(key);
            return;
        }

        DedupPreserveOrder(cands);

        if (op == L'=')
        {
            if (cands.empty())
                m_map.erase(key); // '=' with no entries removes, same as '-'
            else
                m_map[key] = std::move(cands);
            return;
        }

        // op == '+': prepend the new characters ahead of any existing ones.
        auto it = m_map.find(key);
        if (it == m_map.end())
        {
            if (!cands.empty())
                m_map[key] = std::move(cands);
        }
        else
        {
            cands.insert(cands.end(), it->second.begin(), it->second.end());
            DedupPreserveOrder(cands);
            it->second = std::move(cands);
        }
    });

    Rebuild();
    return true;
}

void Dictionary::Rebuild()
{
    // Rebuild the sorted key list (KeysWithPrefix binary-searches it) and the
    // longest-key length (used by the segmenter) from the current map.
    m_maxKeyLen = 0;
    m_sortedKeys.clear();
    m_sortedKeys.reserve(m_map.size());
    for (const auto& kv : m_map)
    {
        m_sortedKeys.push_back(kv.first);
        if (kv.first.size() > m_maxKeyLen)
            m_maxKeyLen = kv.first.size();
    }
    std::sort(m_sortedKeys.begin(), m_sortedKeys.end());
}

bool Dictionary::Reload()
{
    if (m_path.empty())
        return false;
    std::wstring overridePath = m_overridePath;
    bool ok = Load(m_path); // clears m_overridePath
    if (!overridePath.empty())
        ApplyOverrides(overridePath);
    return ok;
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
