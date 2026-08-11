// DictProtocol.h
//
// The tiny request/response protocol spoken between the IME (client) and the
// dictionary service (canto-dictd). One message-mode named pipe per user; each
// message is length-framed by the pipe itself. Header-only so both the DLLs and
// the service share one definition.
//
// Requests:  [u32 op][payload]
// Responses: [u32 status][payload]
//
//   OP_SETDICT   payload = path:wstr, overrides:list<(key:wstr, cands:list<wstr>)>
//                Loads that dictionary + overrides (a no-op if already current).
//                Response: status only.
//   OP_RESOLVE   payload = romanization:wstr
//                Response: status, then candidates:list<(display:wstr, commit:wstr)>
//   OP_PING      Response: status (STATUS_OK once a dictionary is loaded).

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <sddl.h>       // ConvertSidToStringSidW

#include <string>
#include <vector>
#include <utility>
#include <cstring>

namespace dictproto {

enum Op : unsigned { OP_SETDICT = 1, OP_RESOLVE = 2, OP_PING = 3 };
enum Status : unsigned { STATUS_OK = 0, STATUS_NOTLOADED = 1, STATUS_ERR = 2 };

using Override  = std::pair<std::wstring, std::vector<std::wstring>>;
using Candidate = std::pair<std::wstring, std::wstring>; // (display, commit)

// Largest single message we will accept (guards against a corrupt length).
static const size_t kMaxMessage = 32u * 1024u * 1024u;

// ---- Serialization into a byte buffer ---------------------------------------

inline void PutU32(std::vector<char>& b, unsigned v)
{
    b.insert(b.end(), reinterpret_cast<char*>(&v), reinterpret_cast<char*>(&v) + 4);
}

inline void PutWStr(std::vector<char>& b, const std::wstring& s)
{
    PutU32(b, static_cast<unsigned>(s.size()));
    if (!s.empty())
        b.insert(b.end(), reinterpret_cast<const char*>(s.data()),
                 reinterpret_cast<const char*>(s.data()) + s.size() * sizeof(wchar_t));
}

// ---- Deserialization with bounds checks -------------------------------------

struct Reader
{
    const char* p = nullptr;
    size_t n = 0;
    size_t pos = 0;
    bool bad = false;

    Reader(const std::vector<char>& b) : p(b.data()), n(b.size()) {}

    bool need(size_t k) { if (bad || pos + k > n) { bad = true; return false; } return true; }

    unsigned u32()
    {
        if (!need(4)) return 0u;
        unsigned v; std::memcpy(&v, p + pos, 4); pos += 4; return v;
    }

    std::wstring wstr()
    {
        unsigned len = u32();
        if (bad || !need(static_cast<size_t>(len) * sizeof(wchar_t))) return std::wstring();
        std::wstring s(len, L'\0');
        if (len) std::memcpy(&s[0], p + pos, static_cast<size_t>(len) * sizeof(wchar_t));
        pos += static_cast<size_t>(len) * sizeof(wchar_t);
        return s;
    }
};

inline void PutOverrides(std::vector<char>& b, const std::vector<Override>& ovr)
{
    PutU32(b, static_cast<unsigned>(ovr.size()));
    for (const Override& o : ovr)
    {
        PutWStr(b, o.first);
        PutU32(b, static_cast<unsigned>(o.second.size()));
        for (const std::wstring& c : o.second)
            PutWStr(b, c);
    }
}

// Returns false on a malformed buffer.
inline bool GetOverrides(Reader& r, std::vector<Override>& out)
{
    unsigned count = r.u32();
    if (r.bad || count > 1000000u) return false;
    out.clear();
    out.reserve(count);
    for (unsigned i = 0; i < count && !r.bad; ++i)
    {
        std::wstring key = r.wstr();
        unsigned cc = r.u32();
        if (r.bad || cc > 100000u) return false;
        std::vector<std::wstring> cands;
        cands.reserve(cc);
        for (unsigned j = 0; j < cc && !r.bad; ++j)
            cands.push_back(r.wstr());
        out.emplace_back(std::move(key), std::move(cands));
    }
    return !r.bad;
}

inline void PutCandidates(std::vector<char>& b, const std::vector<Candidate>& cands)
{
    PutU32(b, static_cast<unsigned>(cands.size()));
    for (const Candidate& c : cands)
    {
        PutWStr(b, c.first);
        PutWStr(b, c.second);
    }
}

inline bool GetCandidates(Reader& r, std::vector<Candidate>& out)
{
    unsigned count = r.u32();
    if (r.bad || count > 1000000u) return false;
    out.clear();
    out.reserve(count);
    for (unsigned i = 0; i < count && !r.bad; ++i)
    {
        std::wstring display = r.wstr();
        std::wstring commit  = r.wstr();
        out.emplace_back(std::move(display), std::move(commit));
    }
    return !r.bad;
}

// ---- Per-user pipe name ------------------------------------------------------

// A per-user pipe name so different users on one machine do not collide. Falls
// back to a fixed name if the SID cannot be read.
inline std::wstring PipeName()
{
    std::wstring sid;
    HANDLE tok = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok))
    {
        DWORD len = 0;
        GetTokenInformation(tok, TokenUser, nullptr, 0, &len);
        if (len)
        {
            std::vector<char> buf(len);
            if (GetTokenInformation(tok, TokenUser, buf.data(), len, &len))
            {
                TOKEN_USER* tu = reinterpret_cast<TOKEN_USER*>(buf.data());
                LPWSTR s = nullptr;
                if (ConvertSidToStringSidW(tu->User.Sid, &s))
                {
                    sid = s;
                    LocalFree(s);
                }
            }
        }
        CloseHandle(tok);
    }
    if (sid.empty())
        sid = L"default";
    return L"\\\\.\\pipe\\CantoIME.dict." + sid;
}

// ---- Whole-message pipe I/O (message-mode pipe) -----------------------------

// Writes one framed message. Returns false on failure.
inline bool WriteMessage(HANDLE pipe, const std::vector<char>& msg)
{
    DWORD written = 0;
    const char* p = msg.data();
    DWORD total = static_cast<DWORD>(msg.size());
    // In message mode a single WriteFile is one message; loop only for partial
    // writes (rare) to be safe.
    DWORD off = 0;
    while (off < total)
    {
        if (!WriteFile(pipe, p + off, total - off, &written, nullptr) || written == 0)
            return false;
        off += written;
    }
    return true;
}

// Reads one framed message (message mode: assembles across ERROR_MORE_DATA).
// Returns false on failure or an over-long message.
inline bool ReadMessage(HANDLE pipe, std::vector<char>& out)
{
    out.clear();
    char chunk[8192];
    for (;;)
    {
        DWORD read = 0;
        BOOL ok = ReadFile(pipe, chunk, sizeof(chunk), &read, nullptr);
        if (read)
        {
            if (out.size() + read > kMaxMessage)
                return false;
            out.insert(out.end(), chunk, chunk + read);
        }
        if (ok)
            return true; // full message delivered
        if (GetLastError() != ERROR_MORE_DATA)
            return false; // real error or closed
        // else: more of this message remains; keep reading
    }
}

} // namespace dictproto
