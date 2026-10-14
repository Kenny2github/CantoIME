// Dictionary.cpp

#include "Dictionary.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cwctype>
#include <algorithm>
#include <unordered_set>
#include <cstring>

// ---- Cache file format ------------------------------------------------------
//
// The cache is built for read-only memory mapping: a lookup binary-searches a
// small page index, then touches exactly one 4 KB data page, so a switch-in
// only faults in the pages it actually uses instead of reading the whole file.
// All integers are little-endian; strings are UTF-16 (wchar_t) with no
// terminator, so keys compare in place against std::wstring and candidates
// need no UTF-8 decode. Every region starts on a page boundary.
//
//   page 0        CacheHeader (rest of the page is zero padding)
//   page 1..      index: blockCount x IndexEntry, then the first-key blob
//                 (each block's first key, UTF-16), padded to a page boundary
//   dataOffset..  data: dataPages pages, grouped into blocks
//
// A block is normally one page packed with as many whole entries (in sorted key
// order) as fit; an entry never straddles a page. An entry too big for an empty
// page gets a block of its own spanning as many whole pages as it needs (the
// index records pageCount > 1). Block layout:
//
//   u16 count, u16 reserved, u16 offset[count]   (offsets from block start)
//   entry: u16 keyLen, u16 candCount, wchar_t key[keyLen],
//          candCount x (u16 len, wchar_t chars[len])
//
// The cache is a trust boundary (a stale or damaged file must never crash the
// host app): the header and the whole index are validated when mapped, and
// every in-block offset and length is bounds-checked on access.

static const uint32_t kPage = 4096;
static const char     kCacheMagic[8] = { 'C', 'A', 'N', 'T', 'O', 'M', 'A', 'P' };
static const uint32_t kCacheVersion = 2;

struct CacheHeader
{
    char     magic[8];
    uint32_t version;
    uint32_t pageSize;
    uint64_t srcSize;      // source .txt size ...
    uint64_t srcMtime;     // ... and last-write time; mismatch = stale
    uint32_t entryCount;
    uint32_t blockCount;
    uint32_t dataPages;
    uint32_t indexOffset;  // always kPage
    uint32_t indexKeyChars; // first-key blob length, in wchar_t
    uint32_t dataOffset;   // page-aligned
    uint32_t maxKeyLen;
    uint32_t reserved;
};
static_assert(sizeof(CacheHeader) == 64, "cache header layout");

struct IndexEntry
{
    uint32_t firstPage; // block's first data page (relative to dataOffset)
    uint16_t pageCount; // 1, or more for an oversized entry
    uint16_t keyLen;    // block's first key, in the first-key blob
    uint32_t keyOffset; // in wchar_t
};
static_assert(sizeof(IndexEntry) == 12, "cache index layout");

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

// Same ordering as std::wstring::compare, on raw (mapped) characters.
static int CompareKeys(const wchar_t* a, size_t na, const wchar_t* b, size_t nb)
{
    int c = std::char_traits<wchar_t>::compare(a, b, (std::min)(na, nb));
    if (c != 0)
        return c;
    return na < nb ? -1 : (na > nb ? 1 : 0);
}

static uint16_t ReadU16(const uint8_t* p)
{
    uint16_t v;
    std::memcpy(&v, p, 2);
    return v;
}

// Stock dictionaries: a machine-wide cache next to the file.
static std::wstring SiblingCachePath(const std::wstring& txt)
{
    return txt + L".cache";
}

// Per-user cache: %LOCALAPPDATA%\CantoIME\cache\<hash>.cache, where <hash> is
// an FNV-1a-64 of the lowercased path. Creates the folders if `create` is set.
// Empty if %LOCALAPPDATA% cannot be resolved.
static std::wstring AppDataCachePath(const std::wstring& txt, bool create)
{
    wchar_t buf[MAX_PATH];
    DWORD n = ExpandEnvironmentStringsW(L"%LOCALAPPDATA%", buf, MAX_PATH);
    if (n == 0 || n > MAX_PATH || buf[0] == L'%')
        return std::wstring();

    std::wstring dir = std::wstring(buf) + L"\\CantoIME";
    if (create) CreateDirectoryW(dir.c_str(), nullptr);
    dir += L"\\cache";
    if (create) CreateDirectoryW(dir.c_str(), nullptr);

    unsigned long long h = 1469598103934665603ULL;
    for (wchar_t c : txt)
    {
        h ^= static_cast<unsigned long long>(static_cast<unsigned short>(towlower(c)));
        h *= 1099511628211ULL;
    }
    const wchar_t* hex = L"0123456789abcdef";
    std::wstring name(16, L'0');
    for (int i = 15; i >= 0; --i) { name[i] = hex[h & 0xF]; h >>= 4; }
    return dir + L"\\" + name + L".cache";
}

bool Dictionary::FileSignature(const std::wstring& path, Signature& sig)
{
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad))
        return false;
    sig.size  = (static_cast<uint64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
    sig.mtime = (static_cast<uint64_t>(fad.ftLastWriteTime.dwHighDateTime) << 32) |
                fad.ftLastWriteTime.dwLowDateTime;
    return true;
}

bool Dictionary::ParseText(const std::wstring& path)
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

    // Sorted key list for prefix scans (and the cache writer's entry order).
    m_sortedKeys.reserve(m_map.size());
    for (const auto& kv : m_map)
        m_sortedKeys.push_back(kv.first);
    std::sort(m_sortedKeys.begin(), m_sortedKeys.end());
    return true;
}

static void PutU16(std::vector<uint8_t>& b, size_t v)
{
    uint16_t x = static_cast<uint16_t>(v);
    b.insert(b.end(), reinterpret_cast<uint8_t*>(&x), reinterpret_cast<uint8_t*>(&x) + 2);
}

static void PutChars(std::vector<uint8_t>& b, const std::wstring& s)
{
    const uint8_t* p = reinterpret_cast<const uint8_t*>(s.data());
    b.insert(b.end(), p, p + s.size() * sizeof(wchar_t));
}

bool Dictionary::WriteCacheFile(const std::wstring& cachePath, const Signature& sig) const
{
    if (cachePath.empty())
        return false;

    struct Block { uint32_t firstPage; uint32_t pageCount; const std::wstring* firstKey; };
    std::vector<Block> blocks;
    std::vector<uint8_t> data; // data pages

    // Entries waiting to be packed into the current page.
    std::vector<std::vector<uint8_t>> page;
    const std::wstring* pageFirst = nullptr;
    size_t pageBytes = 4; // block header

    // Lays out a block: header, offset table, entries, zero padding.
    auto emitBlock = [&](const std::vector<std::vector<uint8_t>>& entries,
                         uint32_t pageCount, const std::wstring* firstKey)
    {
        size_t base = data.size();
        data.resize(base + static_cast<size_t>(pageCount) * kPage, 0);
        uint8_t* p = &data[base];
        uint16_t n = static_cast<uint16_t>(entries.size());
        std::memcpy(p, &n, 2);
        size_t off = 4 + 2 * entries.size();
        for (size_t i = 0; i < entries.size(); ++i)
        {
            uint16_t o = static_cast<uint16_t>(off);
            std::memcpy(p + 4 + 2 * i, &o, 2);
            std::memcpy(p + off, entries[i].data(), entries[i].size());
            off += entries[i].size();
        }
        blocks.push_back({ static_cast<uint32_t>(base / kPage), pageCount, firstKey });
    };
    auto flushPage = [&]()
    {
        if (page.empty())
            return;
        emitBlock(page, 1, pageFirst);
        page.clear();
        pageBytes = 4;
    };

    for (const std::wstring& key : m_sortedKeys)
    {
        auto it = m_map.find(key);
        if (it == m_map.end())
            continue;
        const std::vector<std::wstring>& cands = it->second;
        if (key.size() > 0xFFFF || cands.size() > 0xFFFF)
            return false;

        std::vector<uint8_t> e;
        PutU16(e, key.size());
        PutU16(e, cands.size());
        PutChars(e, key);
        for (const std::wstring& c : cands)
        {
            if (c.size() > 0xFFFF)
                return false;
            PutU16(e, c.size());
            PutChars(e, c);
        }

        // Pack into the current page if the entry and its offset slot fit.
        if (pageBytes + 2 + e.size() > kPage)
            flushPage();
        if (pageBytes + 2 + e.size() <= kPage)
        {
            if (page.empty())
                pageFirst = &key;
            pageBytes += 2 + e.size();
            page.push_back(std::move(e));
            continue;
        }

        // Too big for an empty page: a run of whole pages holding only it.
        size_t pages = (6 + e.size() + kPage - 1) / kPage;
        if (pages > 0xFFFF)
            return false;
        std::vector<std::vector<uint8_t>> one;
        one.push_back(std::move(e));
        emitBlock(one, static_cast<uint32_t>(pages), &key);
    }
    flushPage();

    // Header page, then the index (entries + first-key blob), page-aligned.
    std::vector<uint8_t> file(kPage, 0);
    std::vector<uint8_t> keyBlob;
    for (const Block& b : blocks)
    {
        IndexEntry ie;
        ie.firstPage = b.firstPage;
        ie.pageCount = static_cast<uint16_t>(b.pageCount);
        ie.keyLen    = static_cast<uint16_t>(b.firstKey->size());
        ie.keyOffset = static_cast<uint32_t>(keyBlob.size() / sizeof(wchar_t));
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&ie);
        file.insert(file.end(), p, p + sizeof(ie));
        PutChars(keyBlob, *b.firstKey);
    }
    file.insert(file.end(), keyBlob.begin(), keyBlob.end());
    file.resize((file.size() + kPage - 1) / kPage * kPage, 0);
    size_t dataOffset = file.size();
    if (dataOffset + data.size() > 0x7FFFFFFF)
        return false;
    file.insert(file.end(), data.begin(), data.end());

    CacheHeader h = {};
    std::memcpy(h.magic, kCacheMagic, sizeof(h.magic));
    h.version       = kCacheVersion;
    h.pageSize      = kPage;
    h.srcSize       = sig.size;
    h.srcMtime      = sig.mtime;
    h.entryCount    = static_cast<uint32_t>(m_map.size());
    h.blockCount    = static_cast<uint32_t>(blocks.size());
    h.dataPages     = static_cast<uint32_t>(data.size() / kPage);
    h.indexOffset   = kPage;
    h.indexKeyChars = static_cast<uint32_t>(keyBlob.size() / sizeof(wchar_t));
    h.dataOffset    = static_cast<uint32_t>(dataOffset);
    h.maxKeyLen     = static_cast<uint32_t>(m_maxKeyLen);
    std::memcpy(file.data(), &h, sizeof(h));

    // Write a uniquely named temp file, then rename it over the cache, so a
    // reader never sees a torn file and a process that has the old cache mapped
    // keeps its (unchanged) view.
    std::wstring tmp = cachePath + L"." + std::to_wstring(GetCurrentProcessId()) +
                       L"." + std::to_wstring(GetCurrentThreadId()) + L".tmp";
    HANDLE hf = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    BOOL ok = WriteFile(hf, file.data(), static_cast<DWORD>(file.size()), &written, nullptr);
    CloseHandle(hf);
    if (!ok || written != file.size())
    {
        DeleteFileW(tmp.c_str());
        return false;
    }
    if (!MoveFileExW(tmp.c_str(), cachePath.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        // Windows refuses to replace a file that another process has mapped.
        // Renaming it aside is still allowed (readers open it with
        // FILE_SHARE_DELETE), so move it out of the way and drop it once no one
        // maps it any more (the delete simply fails until then).
        std::wstring old = cachePath + L".old";
        DeleteFileW(old.c_str());
        if (!MoveFileExW(cachePath.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING) ||
            !MoveFileExW(tmp.c_str(), cachePath.c_str(), 0))
        {
            DeleteFileW(tmp.c_str());
            return false;
        }
        DeleteFileW(old.c_str());
    }
    return true;
}

bool Dictionary::MapCacheFile(const std::wstring& cachePath, const Signature& sig)
{
    if (cachePath.empty())
        return false;

    HANDLE hf = CreateFileW(cachePath.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(hf, &sz) || sz.QuadPart < kPage || sz.QuadPart % kPage != 0 ||
        sz.QuadPart > 0x7FFFFFFF)
    {
        CloseHandle(hf);
        return false;
    }
    const uint64_t fileSize = static_cast<uint64_t>(sz.QuadPart);

    HANDLE hm = CreateFileMappingW(hf, nullptr, PAGE_READONLY, 0, 0, nullptr);
    CloseHandle(hf);
    if (!hm)
        return false;
    const void* v = MapViewOfFile(hm, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(hm); // the view keeps the section alive
    if (!v)
        return false;
    std::shared_ptr<const uint8_t> view(static_cast<const uint8_t*>(v),
                                        [](const uint8_t* p) { UnmapViewOfFile(p); });
    const uint8_t* base = view.get();

    CacheHeader h;
    std::memcpy(&h, base, sizeof(h));
    if (std::memcmp(h.magic, kCacheMagic, sizeof(h.magic)) != 0 ||
        h.version != kCacheVersion || h.pageSize != kPage)
        return false;
    if (h.srcSize != sig.size || h.srcMtime != sig.mtime)
        return false; // the .txt changed since this cache was built

    // Region bounds (64-bit math so a hostile header cannot overflow).
    uint64_t indexEnd = static_cast<uint64_t>(h.indexOffset) +
                        static_cast<uint64_t>(h.blockCount) * sizeof(IndexEntry) +
                        static_cast<uint64_t>(h.indexKeyChars) * sizeof(wchar_t);
    if (h.indexOffset != kPage || h.dataOffset % kPage != 0 || indexEnd > h.dataOffset ||
        static_cast<uint64_t>(h.dataOffset) + static_cast<uint64_t>(h.dataPages) * kPage != fileSize)
        return false;

    // Validate the whole index once (it is small): blocks tile the data region
    // in order, and first keys are in bounds and strictly increasing.
    const uint8_t* index = base + h.indexOffset;
    const wchar_t* keys = reinterpret_cast<const wchar_t*>(
        index + static_cast<size_t>(h.blockCount) * sizeof(IndexEntry));
    uint64_t nextPage = 0;
    const wchar_t* prevKey = nullptr;
    size_t prevLen = 0;
    for (uint32_t b = 0; b < h.blockCount; ++b)
    {
        IndexEntry ie;
        std::memcpy(&ie, index + static_cast<size_t>(b) * sizeof(IndexEntry), sizeof(ie));
        if (ie.pageCount == 0 || ie.firstPage != nextPage || ie.keyLen == 0 ||
            static_cast<uint64_t>(ie.keyOffset) + ie.keyLen > h.indexKeyChars)
            return false;
        nextPage += ie.pageCount;
        const wchar_t* k = keys + ie.keyOffset;
        if (prevKey && CompareKeys(prevKey, prevLen, k, ie.keyLen) >= 0)
            return false;
        prevKey = k;
        prevLen = ie.keyLen;
    }
    if (nextPage != h.dataPages)
        return false;

    // Commit: drop any text-parse form and serve queries from the mapping.
    m_map.clear();
    m_sortedKeys.clear();
    m_view       = std::move(view);
    m_index      = index;
    m_indexKeys  = keys;
    m_data       = base + h.dataOffset;
    m_blockCount = h.blockCount;
    m_entryCount = h.entryCount;
    m_maxKeyLen  = (std::max)(static_cast<size_t>(h.maxKeyLen), m_maxKeyLen);
    return true;
}

uint32_t Dictionary::BlockFor(const std::wstring& key) const
{
    // First block whose first key is > key, minus one. UINT32_MAX if key sorts
    // before every block.
    uint32_t lo = 0, hi = m_blockCount;
    while (lo < hi)
    {
        uint32_t mid = lo + (hi - lo) / 2;
        IndexEntry ie;
        std::memcpy(&ie, m_index + static_cast<size_t>(mid) * sizeof(IndexEntry), sizeof(ie));
        if (CompareKeys(m_indexKeys + ie.keyOffset, ie.keyLen, key.data(), key.size()) <= 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo == 0 ? UINT32_MAX : lo - 1;
}

uint32_t Dictionary::BlockEntryCount(uint32_t block) const
{
    IndexEntry ie;
    std::memcpy(&ie, m_index + static_cast<size_t>(block) * sizeof(IndexEntry), sizeof(ie));
    const uint8_t* p = m_data + static_cast<size_t>(ie.firstPage) * kPage;
    uint32_t n = ReadU16(p);
    if (4 + 2 * static_cast<size_t>(n) > static_cast<size_t>(ie.pageCount) * kPage)
        return 0;
    return n;
}

bool Dictionary::BlockEntry(uint32_t block, uint32_t i, Entry& e) const
{
    IndexEntry ie;
    std::memcpy(&ie, m_index + static_cast<size_t>(block) * sizeof(IndexEntry), sizeof(ie));
    const uint8_t* p = m_data + static_cast<size_t>(ie.firstPage) * kPage;
    const size_t bytes = static_cast<size_t>(ie.pageCount) * kPage;
    const size_t n = ReadU16(p);
    if (i >= n || 4 + 2 * n > bytes)
        return false;
    size_t off = ReadU16(p + 4 + 2 * static_cast<size_t>(i));
    if (off < 4 + 2 * n || (off & 1) || off + 4 > bytes)
        return false;
    e.keyLen    = ReadU16(p + off);
    e.candCount = ReadU16(p + off + 2);
    if (off + 4 + 2 * static_cast<size_t>(e.keyLen) > bytes)
        return false;
    e.key   = reinterpret_cast<const wchar_t*>(p + off + 4);
    e.cands = p + off + 4 + 2 * static_cast<size_t>(e.keyLen);
    e.end   = p + bytes;
    return true;
}

void Dictionary::DecodeCandidates(const Entry& e, std::vector<std::wstring>& out)
{
    out.clear();
    out.reserve(e.candCount);
    const uint8_t* q = e.cands;
    for (uint32_t c = 0; c < e.candCount; ++c)
    {
        if (e.end - q < 2)
            break;
        size_t len = ReadU16(q);
        if (static_cast<size_t>(e.end - q) < 2 + 2 * len)
            break; // damaged entry: keep what decoded cleanly
        out.emplace_back(reinterpret_cast<const wchar_t*>(q + 2), len);
        q += 2 + 2 * len;
    }
}

bool Dictionary::BaseLookup(const std::wstring& key, std::vector<std::wstring>* out) const
{
    if (!m_view)
    {
        auto it = m_map.find(key);
        if (it == m_map.end())
            return false;
        if (out)
            *out = it->second;
        return true;
    }

    uint32_t b = BlockFor(key);
    if (b == UINT32_MAX)
        return false;
    uint32_t lo = 0, hi = BlockEntryCount(b);
    while (lo < hi)
    {
        uint32_t mid = lo + (hi - lo) / 2;
        Entry e;
        if (!BlockEntry(b, mid, e))
            return false;
        int c = CompareKeys(e.key, e.keyLen, key.data(), key.size());
        if (c == 0)
        {
            if (out)
                DecodeCandidates(e, *out);
            return true;
        }
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return false;
}

void Dictionary::BaseKeysWithPrefix(const std::wstring& lo, size_t maxKeys,
                                    std::vector<std::wstring>& out) const
{
    if (!m_view)
    {
        // First key >= prefix; every key sharing the prefix is contiguous from here.
        auto it = std::lower_bound(m_sortedKeys.begin(), m_sortedKeys.end(), lo);
        for (; it != m_sortedKeys.end() && out.size() < maxKeys; ++it)
        {
            if (it->compare(0, lo.size(), lo) != 0)
                break;
            out.push_back(*it);
        }
        return;
    }

    // Start in the block that would hold the prefix itself, at its first entry
    // >= prefix, then walk forward across block boundaries while keys match.
    uint32_t b = BlockFor(lo);
    if (b == UINT32_MAX)
        b = 0;
    uint32_t i = 0, hi = BlockEntryCount(b);
    while (i < hi)
    {
        uint32_t mid = i + (hi - i) / 2;
        Entry e;
        if (!BlockEntry(b, mid, e))
            return;
        if (CompareKeys(e.key, e.keyLen, lo.data(), lo.size()) < 0)
            i = mid + 1;
        else
            hi = mid;
    }
    for (; b < m_blockCount; ++b, i = 0)
    {
        uint32_t n = BlockEntryCount(b);
        for (; i < n; ++i)
        {
            Entry e;
            if (!BlockEntry(b, i, e))
                return;
            if (e.keyLen < lo.size() ||
                std::char_traits<wchar_t>::compare(e.key, lo.data(), lo.size()) != 0)
                return;
            out.emplace_back(e.key, e.keyLen);
            if (out.size() >= maxKeys)
                return;
        }
    }
}

bool Dictionary::Load(const std::wstring& path, bool machineWide)
{
    *this = Dictionary(); // drops any previous mapping, parse, and overrides
    m_path = path;

    // Map the cache. Stock dictionaries carry a machine-wide sibling cache
    // (built by the installer); custom ones use only the per-user cache.
    Signature sig;
    bool haveSig = FileSignature(path, sig);
    if (haveSig)
    {
        if (machineWide && MapCacheFile(SiblingCachePath(path), sig))
            return true;
        if (MapCacheFile(AppDataCachePath(path, false), sig))
            return true;
    }

    // No usable cache: parse the text, then write the cache so the next load
    // (in any process) maps it. Switch this instance over to the mapping too,
    // which frees the parsed map's memory in the host process.
    if (!ParseText(path))
        return false;
    if (haveSig)
    {
        std::wstring sibling = machineWide ? SiblingCachePath(path) : std::wstring();
        if (WriteCacheFile(sibling, sig))
            MapCacheFile(sibling, sig);
        else
        {
            std::wstring perUser = AppDataCachePath(path, true);
            if (WriteCacheFile(perUser, sig))
                MapCacheFile(perUser, sig);
        }
    }
    return true;
}

bool Dictionary::PrebuildCache(const std::wstring& txtPath, bool machineWide)
{
    Signature sig;
    if (!FileSignature(txtPath, sig))
        return false;

    Dictionary d;
    if (machineWide && d.MapCacheFile(SiblingCachePath(txtPath), sig))
        return true;
    if (d.MapCacheFile(AppDataCachePath(txtPath, false), sig))
        return true;

    if (!d.ParseText(txtPath))
        return false;
    // Stock: prefer the sibling (machine-wide, readable by every user), and
    // fall back to %LOCALAPPDATA% only if that folder is read-only. Custom:
    // always per-user; never write next to the (per-user) dictionary.
    if (machineWide && d.WriteCacheFile(SiblingCachePath(txtPath), sig))
        return true;
    return d.WriteCacheFile(AppDataCachePath(txtPath, true), sig);
}

void Dictionary::ApplyOverrides(
    const std::vector<std::pair<std::wstring, std::vector<std::wstring>>>& overrides)
{
    m_over.clear();
    m_overKeys.clear();
    for (const auto& ov : overrides)
    {
        std::wstring key = ToLower(ov.first);
        if (key.empty() || ov.second.empty())
            continue;

        // Later overrides for the same key go first, as when they were merged
        // into the dictionary one by one.
        std::vector<std::wstring>& vec = m_over[key];
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
    }
    for (const auto& kv : m_over)
        m_overKeys.push_back(kv.first);
    std::sort(m_overKeys.begin(), m_overKeys.end());
}

std::vector<std::wstring> Dictionary::Lookup(const std::wstring& romanization) const
{
    std::wstring key = ToLower(romanization);
    std::vector<std::wstring> base;
    BaseLookup(key, &base);

    auto it = m_over.find(key);
    if (it == m_over.end())
        return base;

    // Override candidates first so the user's extras take the low positions
    // (the first is pickable with the 1 key); a stock character the override
    // repeats surfaces only at the front, not twice.
    std::vector<std::wstring> merged = it->second;
    std::unordered_set<std::wstring> seen(merged.begin(), merged.end());
    for (std::wstring& c : base)
        if (seen.insert(c).second)
            merged.push_back(std::move(c));
    return merged;
}

bool Dictionary::HasExact(const std::wstring& romanization) const
{
    std::wstring key = ToLower(romanization);
    return m_over.count(key) != 0 || BaseLookup(key, nullptr);
}

std::vector<std::wstring> Dictionary::KeysWithPrefix(const std::wstring& prefix,
                                                     size_t maxKeys) const
{
    std::vector<std::wstring> out;
    if (prefix.empty() || maxKeys == 0)
        return out;

    std::wstring lo = ToLower(prefix);
    BaseKeysWithPrefix(lo, maxKeys, out);
    if (m_overKeys.empty())
        return out;

    // Merge in override-only keys. The base list already holds its first
    // maxKeys matches, so the merged head is exact.
    auto it = std::lower_bound(m_overKeys.begin(), m_overKeys.end(), lo);
    for (; it != m_overKeys.end() && it->compare(0, lo.size(), lo) == 0; ++it)
        out.push_back(*it);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    if (out.size() > maxKeys)
        out.resize(maxKeys);
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
        std::wstring sub = s.substr(start, len);
        if (m_over.count(sub) != 0 || BaseLookup(sub, nullptr))
            return len;
    }
    return 0;
}
