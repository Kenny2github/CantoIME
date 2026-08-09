// Dictionary.h
//
// Runtime romanization -> character dictionary.
//
// The dictionary is a plain UTF-8 text file. Each line is:
//     romanization<TAB or spaces>char1 char2 char3 ...
// Lines beginning with '#' (after optional leading whitespace) are comments.
// A UTF-8 BOM at the start of the file is tolerated.
//
// A second, optional file can override the stock dictionary after it loads
// (see ApplyOverrides). Each of its lines starts with an operator:
//     +key char1 char2 ...   prepend these characters to the key's entries
//     =key char1 char2 ...   replace the key's entries entirely (no chars = remove)
//     -key                   remove the key entirely
// Whitespace between the operator and the key is optional.

#pragma once

#include <string>
#include <vector>
#include <unordered_map>

class Dictionary
{
public:
    Dictionary() = default;

    // Loads (or reloads) the dictionary from the given UTF-8 file.
    // Returns true if the file was opened and parsed (even if empty).
    bool Load(const std::wstring& path);

    // Applies an override file on top of the currently loaded dictionary using
    // the +/=/- line grammar documented at the top of this header. Missing keys
    // referenced by '+' are created; missing files are a no-op. Returns true if
    // the file was opened and parsed. The path is remembered so Reload() reapplies
    // it. Call after Load().
    bool ApplyOverrides(const std::wstring& path);

    // Reloads from the last successfully loaded path, then reapplies the last
    // override path if one was set. Returns false if we have never loaded.
    bool Reload();

    // Looks up an exact romanization key. Returns the candidate list, or an empty
    // vector if the key is not present. The comparison is case-insensitive
    // (keys are stored lowercased).
    const std::vector<std::wstring>& Lookup(const std::wstring& romanization) const;

    // True if the exact key exists.
    bool HasExact(const std::wstring& romanization) const;

    // Returns keys (in sorted order) that begin with the given prefix, up to
    // maxKeys results. An empty prefix returns nothing. Case-insensitive.
    std::vector<std::wstring> KeysWithPrefix(const std::wstring& prefix,
                                             size_t maxKeys) const;

    // Length of the longest exact key that is a prefix of s starting at
    // position `start`, or 0 if none. Used by the phrase segmenter to lock
    // complete leading syllables before prefix-matching the final one.
    size_t LongestExactPrefixLen(const std::wstring& s, size_t start) const;

    // True if at least one entry is loaded.
    bool IsLoaded() const { return !m_map.empty(); }

    const std::wstring& Path() const { return m_path; }

private:
    // Rebuilds m_sortedKeys and m_maxKeyLen from m_map. Call after any mutation.
    void Rebuild();

    std::wstring m_path;
    std::wstring m_overridePath;
    std::unordered_map<std::wstring, std::vector<std::wstring>> m_map;
    std::vector<std::wstring> m_sortedKeys; // all keys, sorted, for prefix scans
    size_t m_maxKeyLen = 0;                 // longest key length (for segmenter)
    std::vector<std::wstring> m_empty;      // returned by Lookup on miss
};
