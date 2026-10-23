// Dictionary.h
//
// Runtime romanization -> character dictionary.
//
// The dictionary is a plain UTF-8 text file. Each line is:
//     romanization<TAB or spaces>char1 char2 char3 ...
// Lines beginning with '#' (after optional leading whitespace) are comments.
// A UTF-8 BOM at the start of the file is tolerated.
//
// Parsing 10+ MB of text on every switch-in is slow, so a parsed dictionary is
// also written to a binary cache laid out for memory mapping (format described
// in Dictionary.cpp). Load() maps that cache read-only and answers queries in
// place, touching only the page index and the page an entry lives on. A
// missing, stale, or malformed cache falls back to the text parse (and rewrites
// the cache for next time), so a bad cache can never break the IME.

#pragma once

#include <string>
#include <vector>
#include <utility>
#include <unordered_map>
#include <memory>
#include <cstdint>

class Dictionary
{
public:
    Dictionary() = default;

    // Loads (or reloads) the dictionary for the given UTF-8 file: maps its
    // cache if one is valid, else parses the text (and writes the cache).
    // Returns true if the dictionary is usable (even if empty). Clears any
    // overrides. machineWide selects where the cache lives: true (stock
    // dictionaries) uses a sibling "<file>.cache", falling back to
    // %LOCALAPPDATA%\CantoIME if that folder is read-only; false (custom,
    // per-user dictionaries) uses only %LOCALAPPDATA%\CantoIME.
    bool Load(const std::wstring& path, bool machineWide = true);

    // Sets the user overrides consulted on top of the loaded dictionary. They
    // live in a small in-memory overlay (the mapped cache is read-only): for
    // each key the override candidates come first, then the stock ones minus
    // duplicates, and keys with no stock entry are added. Call after Load().
    void ApplyOverrides(
        const std::vector<std::pair<std::wstring, std::vector<std::wstring>>>& overrides);

    // Parses txtPath and writes its cache (see Load for the machineWide
    // policy). A no-op if a valid cache already exists. Returns true if a valid
    // cache exists afterward. Used by canto-mkcache (src/mkcache.cpp).
    static bool PrebuildCache(const std::wstring& txtPath, bool machineWide = true);

    // Looks up an exact romanization key. Returns the candidate list, or an
    // empty vector if the key is not present. Case-insensitive (keys are stored
    // lowercased). Returned by value: candidates are decoded from the mapping.
    std::vector<std::wstring> Lookup(const std::wstring& romanization) const;

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
    bool IsLoaded() const { return m_entryCount != 0 || !m_map.empty() || !m_over.empty(); }

    // True if queries are served from the mapped cache (not a text parse).
    bool IsMapped() const { return m_view != nullptr; }

    const std::wstring& Path() const { return m_path; }

private:
    friend struct DictionaryTest; // src/dicttest.cpp

    // Source file fingerprint stored in the cache header; a mismatch means the
    // text changed and the cache is stale.
    struct Signature { uint64_t size = 0; uint64_t mtime = 0; };
    static bool FileSignature(const std::wstring& path, Signature& sig);

    // Parses the text file into m_map / m_sortedKeys / m_maxKeyLen.
    bool ParseText(const std::wstring& path);

    // Writes m_map (a text parse) as a cache file, atomically. False if the
    // dictionary cannot be represented (a field over 65535) or on I/O failure.
    bool WriteCacheFile(const std::wstring& cachePath, const Signature& sig) const;

    // Maps and validates a cache file. On success the dictionary serves
    // queries from it; on failure (missing, stale, malformed) nothing changes.
    bool MapCacheFile(const std::wstring& cachePath, const Signature& sig);

    // Base-dictionary queries (mapped cache or text parse), without overrides.
    bool BaseLookup(const std::wstring& key, std::vector<std::wstring>* out) const;
    void BaseKeysWithPrefix(const std::wstring& lo, size_t maxKeys,
                            std::vector<std::wstring>& out) const;

    // Mapped-cache helpers. A "block" is one data page, or a multi-page run
    // holding a single oversized entry.
    struct Entry
    {
        const wchar_t* key;
        uint16_t       keyLen;
        uint16_t       candCount;
        const uint8_t* cands; // candCount x (u16 len, wchar_t[len])
        const uint8_t* end;   // end of the block, for bounds checks
    };
    uint32_t BlockFor(const std::wstring& key) const; // last block whose first key <= key
    uint32_t BlockEntryCount(uint32_t block) const;
    bool     BlockEntry(uint32_t block, uint32_t i, Entry& e) const;
    static void DecodeCandidates(const Entry& e, std::vector<std::wstring>& out);

    std::wstring m_path;

    // Text-parse form (used when no valid cache could be mapped).
    std::unordered_map<std::wstring, std::vector<std::wstring>> m_map;
    std::vector<std::wstring> m_sortedKeys; // all keys, sorted, for prefix scans

    // Mapped form. The file and section handles are closed right after mapping
    // (the view keeps the section alive), and the view is unmapped when the
    // last owner goes away, so a Dictionary stays movable and reload/destroy
    // unmaps.
    std::shared_ptr<const uint8_t> m_view;
    const uint8_t* m_index = nullptr;     // block index entries
    const wchar_t* m_indexKeys = nullptr; // first-key blob for the index
    const uint8_t* m_data = nullptr;      // first data page
    uint32_t       m_blockCount = 0;
    uint32_t       m_entryCount = 0;

    size_t m_maxKeyLen = 0; // longest key length, base or override (for segmenter)

    // Override overlay: key -> override candidates (deduped), plus sorted keys.
    std::unordered_map<std::wstring, std::vector<std::wstring>> m_over;
    std::vector<std::wstring> m_overKeys;
};
