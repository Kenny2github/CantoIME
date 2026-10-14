// dicttest.cpp
//
// canto-dicttest.exe <dictionary.txt> [<dictionary2.txt> ...]
//
// The runnable check for the mmap dictionary cache (run in CI). For each
// dictionary, plus a synthetic one with an entry too big for a single page:
//   - builds the cache from the text parse, maps it, and checks every key's
//     candidates, exact-match and longest-prefix answers match the text parse;
//   - checks prefix scans (unbounded per first character, which crosses every
//     page boundary, and capped like the candidate engine for every 1-3
//     character prefix);
//   - checks overrides give the same answers over either form;
//   - checks stale, truncated, and corrupt headers/indexes are rejected, and
//     that garbage inside data pages never crashes a query.
// Exit code 0 if everything passed.

#include "Dictionary.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <set>

struct DictionaryTest
{
    static int failures;

    static void Fail(const char* what, const std::wstring& detail)
    {
        if (++failures <= 30)
            wprintf(L"FAIL: %hs [%ls]\n", what, detail.c_str());
    }

    static std::wstring TempPath(const std::wstring& name)
    {
        wchar_t buf[MAX_PATH];
        GetTempPathW(MAX_PATH, buf);
        return std::wstring(buf) + name;
    }

    static double NowMs()
    {
        LARGE_INTEGER f, t;
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&t);
        return 1000.0 * static_cast<double>(t.QuadPart) / static_cast<double>(f.QuadPart);
    }

    static bool ReadAll(const std::wstring& path, std::vector<char>& out)
    {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return false;
        LARGE_INTEGER sz;
        GetFileSizeEx(h, &sz);
        out.resize(static_cast<size_t>(sz.QuadPart));
        DWORD rd = 0;
        BOOL ok = ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &rd, nullptr);
        CloseHandle(h);
        return ok && rd == out.size();
    }

    static bool WriteAll(const std::wstring& path, const void* data, size_t n)
    {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return false;
        DWORD wr = 0;
        BOOL ok = n == 0 || WriteFile(h, data, static_cast<DWORD>(n), &wr, nullptr);
        CloseHandle(h);
        return ok && (n == 0 || wr == n);
    }

    // Every query the candidate engine makes must answer the same on the
    // mapped cache as on the text parse it was built from.
    static void Compare(const Dictionary& text, const Dictionary& mapped)
    {
        std::set<wchar_t> firsts;
        std::set<std::wstring> prefixes;
        for (const std::wstring& key : text.m_sortedKeys)
        {
            if (mapped.Lookup(key) != text.Lookup(key))
                Fail("lookup differs", key);
            if (!mapped.HasExact(key) || mapped.HasExact(key + L"\x7f"))
                Fail("HasExact wrong", key);
            std::wstring probe = L"x" + key + L"zzq";
            if (mapped.LongestExactPrefixLen(probe, 1) != text.LongestExactPrefixLen(probe, 1))
                Fail("LongestExactPrefixLen differs", key);
            firsts.insert(key[0]);
            for (size_t l = 1; l <= 3 && l <= key.size(); ++l)
                prefixes.insert(key.substr(0, l));
        }

        size_t total = 0;
        for (wchar_t c : firsts)
        {
            std::wstring p(1, c);
            std::vector<std::wstring> a = text.KeysWithPrefix(p, SIZE_MAX);
            std::vector<std::wstring> b = mapped.KeysWithPrefix(p, SIZE_MAX);
            if (a != b)
                Fail("full prefix scan differs", p);
            total += b.size();
        }
        if (total != text.m_sortedKeys.size())
            Fail("full prefix scans did not cover every key", std::to_wstring(total));

        for (const std::wstring& p : prefixes)
            if (text.KeysWithPrefix(p, 64) != mapped.KeysWithPrefix(p, 64))
                Fail("capped prefix scan differs", p);

        for (const wchar_t* miss : { L"\x01", L"\xffff", L"zzzzzzzzzzzzzzzz" })
            if (mapped.HasExact(miss) != text.HasExact(miss) ||
                mapped.KeysWithPrefix(miss, 64) != text.KeysWithPrefix(miss, 64))
                Fail("miss handling differs", miss);
    }

    // Maps a damaged copy of the cache; returns whether it was accepted.
    static bool MapsDamaged(const std::vector<char>& bytes, size_t size, int tag,
                            const Dictionary::Signature& sig)
    {
        std::wstring bad = TempPath(L"canto-dicttest-bad" + std::to_wstring(tag) + L".cache");
        if (!WriteAll(bad, bytes.data(), size))
        {
            Fail("could not write damaged copy", bad);
            return false;
        }
        Dictionary d;
        bool ok = d.MapCacheFile(bad, sig);
        if (ok)
            for (const wchar_t* k : { L"a", L"ngo", L"zz" })
                (void)d.Lookup(k);
        return ok;
    }

    static void Check(const std::wstring& txt, bool expectMultiPage)
    {
        int before = failures;
        Dictionary text;
        double t0 = NowMs();
        if (!text.ParseText(txt) || text.m_sortedKeys.empty())
        {
            Fail("text parse failed", txt);
            return;
        }
        double t1 = NowMs();

        Dictionary::Signature sig;
        Dictionary::FileSignature(txt, sig);
        std::wstring cache = TempPath(L"canto-dicttest.cache");
        if (!text.WriteCacheFile(cache, sig))
        {
            Fail("cache write failed", cache);
            return;
        }

        Dictionary mapped;
        double t2 = NowMs();
        if (!mapped.MapCacheFile(cache, sig) || !mapped.IsMapped())
        {
            Fail("cache map failed", cache);
            return;
        }
        double t3 = NowMs();

        std::vector<char> bytes;
        ReadAll(cache, bytes);
        uint32_t multi = 0;
        for (uint32_t b = 0; b < mapped.m_blockCount; ++b)
        {
            uint16_t pages;
            std::memcpy(&pages, mapped.m_index + b * 12 + 4, 2);
            multi += pages > 1;
        }
        wprintf(L"%ls: %u keys, %u blocks (%u multi-page), %zu bytes; parse %.1f ms, map %.3f ms\n",
                txt.c_str(), mapped.m_entryCount, mapped.m_blockCount, multi, bytes.size(),
                t1 - t0, t3 - t2);
        if (mapped.m_entryCount != text.m_sortedKeys.size())
            Fail("entry count differs", txt);
        if (expectMultiPage && multi == 0)
            Fail("expected a multi-page block", txt);

        Compare(text, mapped);

        // Overrides: same answers over either form (prepend + dedup, new key).
        const std::wstring& k0 = text.m_sortedKeys[text.m_sortedKeys.size() / 2];
        std::vector<std::pair<std::wstring, std::vector<std::wstring>>> ov = {
            { k0, { L"\x6E2C", text.Lookup(k0)[0], L"\x6E2C" } },
            { L"zzzcantotest", { L"\x7532", L"\x4E59" } },
        };
        text.ApplyOverrides(ov);
        mapped.ApplyOverrides(ov);
        for (const std::wstring& k : { k0, std::wstring(L"zzzcantotest") })
            if (mapped.Lookup(k) != text.Lookup(k) || mapped.Lookup(k).empty())
                Fail("override lookup differs", k);
        if (mapped.Lookup(k0)[0] != L"\x6E2C")
            Fail("override not first", k0);
        for (const std::wstring& p : { k0.substr(0, 1), std::wstring(L"zzz") })
            if (mapped.KeysWithPrefix(p, 64) != text.KeysWithPrefix(p, 64))
                Fail("override prefix scan differs", p);
        if (mapped.LongestExactPrefixLen(L"zzzcantotestx", 0) != 12)
            Fail("override key not seen by segmenter", L"zzzcantotest");

        // Stale: the source changed.
        Dictionary::Signature stale = sig;
        stale.mtime += 1;
        if (Dictionary().MapCacheFile(cache, stale))
            Fail("accepted stale cache", txt);

        // Truncated anywhere.
        const size_t n = bytes.size();
        const size_t cuts[] = { n - 1, n - 4096, n / 2, 4096, 64, 0 };
        int tag = 0;
        for (size_t cut : cuts)
            if (MapsDamaged(bytes, cut, tag++, sig))
                Fail("accepted truncated cache", std::to_wstring(cut));

        // Corrupt header / index fields. Offsets follow CacheHeader and
        // IndexEntry in Dictionary.cpp; the index starts at page 1.
        struct Poke { size_t at; uint32_t value; int width; };
        const Poke pokes[] = {
            { 8, 1, 4 },                 // version
            { 36, mapped.m_blockCount + 1, 4 }, // blockCount
            { 40, 0xFFFFFFF, 4 },        // dataPages
            { 4096 + 4, 0, 2 },          // first block pageCount = 0
            { 4096 + 8, 0x7FFFFFF, 4 },  // first block key offset out of range
        };
        for (const Poke& pk : pokes)
        {
            std::vector<char> c = bytes;
            std::memcpy(&c[pk.at], &pk.value, pk.width);
            if (MapsDamaged(c, c.size(), tag++, sig))
                Fail("accepted corrupt header/index", std::to_wstring(pk.at));
        }

        // Garbage inside data pages: still maps (the index is intact), and no
        // query may crash or read out of bounds.
        {
            std::vector<char> c = bytes;
            size_t dataOff = static_cast<size_t>(mapped.m_data - mapped.m_view.get());
            uint32_t seed = 12345;
            for (size_t i = dataOff; i < c.size(); i += (i < dataOff + 3 * 4096) ? 1 : 97)
            {
                seed = seed * 1103515245 + 12345;
                c[i] = static_cast<char>(seed >> 16);
            }
            std::wstring bad = TempPath(L"canto-dicttest-garbage.cache");
            WriteAll(bad, c.data(), c.size());
            Dictionary d;
            if (!d.MapCacheFile(bad, sig))
                Fail("rejected cache with intact index", bad);
            size_t sink = 0;
            for (const std::wstring& key : text.m_sortedKeys)
            {
                sink += d.Lookup(key).size();
                sink += d.LongestExactPrefixLen(key, 0);
            }
            for (wchar_t ch = L'a'; ch <= L'z'; ++ch)
                sink += d.KeysWithPrefix(std::wstring(1, ch), SIZE_MAX).size();
            wprintf(L"  garbage-page sweep survived (%zu)\n", sink);
        }

        wprintf(L"  %ls\n", failures == before ? L"OK" : L"FAILED");
    }

    // A small dictionary with one entry far bigger than a page, between
    // ordinary ones, so a multi-page run sits mid-file.
    static std::wstring WriteSynthetic()
    {
        std::wstring w = L"# synthetic\na \x4E00\n";
        for (int i = 0; i < 1500; ++i)
            w += L"k" + std::to_wstring(1000 + i) + L" \x5B57 \x6587 \x8A9E\n";
        w += L"ji";
        for (int i = 0; i < 5000; ++i)
            w += L" " + std::wstring(1, static_cast<wchar_t>(0x4E00 + i));
        w += L"\njia \x5BB6\nzz \x5B57\n";

        int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                    nullptr, 0, nullptr, nullptr);
        std::string u(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                            &u[0], n, nullptr, nullptr);
        std::wstring path = TempPath(L"canto-dicttest-synthetic.txt");
        WriteAll(path, u.data(), u.size());
        return path;
    }
};

int DictionaryTest::failures = 0;

int wmain(int argc, wchar_t** argv)
{
    DictionaryTest::Check(DictionaryTest::WriteSynthetic(), true);
    for (int i = 1; i < argc; ++i)
        DictionaryTest::Check(argv[i], false);

    wprintf(L"%ls (%d failures)\n", DictionaryTest::failures ? L"FAILED" : L"PASSED",
            DictionaryTest::failures);
    return DictionaryTest::failures ? 1 : 0;
}
