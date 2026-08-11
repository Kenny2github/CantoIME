// DictService.cpp  ->  canto-dictd.exe
//
// The CantoIME dictionary service. A per-user, resident, windowless process that
// loads the dictionary once and answers candidate-resolution requests over a
// named pipe (see DictProtocol.h). The IME (each DLL instance) is a client, so
// the multi-megabyte dictionary is parsed once per logon session instead of on
// every switch into the IME.
//
// Lifetime: single instance per user (named mutex). Started at logon (installer
// Run entry) and/or on demand by the IME. Loads the user's currently-selected
// dictionary at startup; a client's SETDICT is authoritative and corrects it if
// the user's variant/choice differs.

#include "Dictionary.h"
#include "CandidateEngine.h"
#include "Settings.h"
#include "Globals.h"        // CANTO_DICT_FILENAME / CANTO_DICT_SIMPLIFIED
#include "DictProtocol.h"

#include <string>
#include <vector>
#include <thread>
#include <mutex>

using namespace dictproto;

// The one loaded dictionary, guarded for concurrent clients. RESOLVE reads it;
// SETDICT (and the startup preload) rebuild it.
static std::mutex             g_mutex;
static Dictionary             g_dict;
static std::wstring           g_loadedPath;
static std::vector<Override>  g_loadedOverrides;

static std::wstring ExeDir()
{
    wchar_t path[MAX_PATH] = { 0 };
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return std::wstring();
    std::wstring d(path);
    size_t slash = d.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : d.substr(0, slash + 1);
}

static std::wstring ExpandEnv(const std::wstring& s)
{
    wchar_t buf[MAX_PATH];
    DWORD n = ExpandEnvironmentStringsW(s.c_str(), buf, MAX_PATH);
    return (n != 0 && n <= MAX_PATH) ? std::wstring(buf) : s;
}

// Resolves the dictionary path from settings the same way the IME does: a custom
// path (env-expanded), or the stock file next to this executable (which the
// installer places alongside the DLLs and dictionaries).
static std::wstring ResolvePathFromSettings(const CantoSettings& s)
{
    if (s.dictSource == DictSource::Custom)
        return ExpandEnv(s.dictionaryPath);
    return ExeDir() + (s.dictSource == DictSource::StockSimplified
                           ? CANTO_DICT_SIMPLIFIED : CANTO_DICT_FILENAME);
}

// (Re)loads the dictionary if the (path, overrides) differ from what is loaded.
static void LoadDictionary(const std::wstring& path, const std::vector<Override>& overrides)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (path == g_loadedPath && overrides == g_loadedOverrides && g_dict.IsLoaded())
        return;
    g_dict.Load(path);
    g_dict.ApplyOverrides(overrides);
    g_loadedPath = path;
    g_loadedOverrides = overrides;
}

// Best-effort preload from the current user's settings, so the dictionary is hot
// before the first switch into the IME.
static void PreloadFromSettings()
{
    CantoSettings s = LoadSettings();
    std::wstring path = ResolvePathFromSettings(s);
    std::vector<Override> overrides =
        LoadOverrides(DictionaryIdentity(s.dictSource, s.dictionaryPath));
    LoadDictionary(path, overrides);
}

// ---- Request handling -------------------------------------------------------

static std::vector<char> HandleSetDict(Reader& r)
{
    std::wstring path = r.wstr();
    std::vector<Override> overrides;
    std::vector<char> resp;
    if (!GetOverrides(r, overrides))
    {
        PutU32(resp, STATUS_ERR);
        return resp;
    }
    LoadDictionary(path, overrides);
    PutU32(resp, STATUS_OK);
    return resp;
}

static std::vector<char> HandleResolve(Reader& r)
{
    std::wstring rom = r.wstr();
    std::vector<char> resp;
    if (r.bad)
    {
        PutU32(resp, STATUS_ERR);
        return resp;
    }

    std::vector<Candidate> out;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_dict.IsLoaded())
        {
            PutU32(resp, STATUS_NOTLOADED);
            PutCandidates(resp, out);
            return resp;
        }
        for (const EngineCandidate& c : BuildCandidates(g_dict, rom))
            out.emplace_back(c.display, c.commit);
    }
    PutU32(resp, STATUS_OK);
    PutCandidates(resp, out);
    return resp;
}

static std::vector<char> Dispatch(const std::vector<char>& msg)
{
    Reader r(msg);
    unsigned op = r.u32();
    switch (op)
    {
    case OP_SETDICT: return HandleSetDict(r);
    case OP_RESOLVE: return HandleResolve(r);
    case OP_PING:
    {
        std::vector<char> resp;
        std::lock_guard<std::mutex> lock(g_mutex);
        PutU32(resp, g_dict.IsLoaded() ? STATUS_OK : STATUS_NOTLOADED);
        return resp;
    }
    default:
    {
        std::vector<char> resp;
        PutU32(resp, STATUS_ERR);
        return resp;
    }
    }
}

// Serves one connected client until it disconnects. A client keeps the pipe open
// and sends many requests (one per keystroke), so loop until the read fails.
static void ServeClient(HANDLE pipe)
{
    std::vector<char> msg;
    while (ReadMessage(pipe, msg))
    {
        std::vector<char> resp = Dispatch(msg);
        if (!WriteMessage(pipe, resp))
            break;
    }
    FlushFileBuffers(pipe);
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
}

static int RunServer()
{
    std::wstring name = PipeName();
    for (;;)
    {
        HANDLE pipe = CreateNamedPipeW(
            name.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            64 * 1024, 64 * 1024, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE)
            return 1; // fatal: cannot create the pipe

        BOOL connected = ConnectNamedPipe(pipe, nullptr)
                             ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (!connected)
        {
            CloseHandle(pipe);
            continue;
        }
        std::thread(ServeClient, pipe).detach();
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    // One service per user: if we are the second, exit and let the first serve.
    std::wstring mutexName = L"Local\\CantoIME.dictd";
    HANDLE mutex = CreateMutexW(nullptr, FALSE, mutexName.c_str());
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS)
        return 0;

    PreloadFromSettings();  // hot dictionary before the first request
    return RunServer();
}
