// DictClient.cpp

#include "DictClient.h"
#include "DictProtocol.h"
#include "Globals.h"   // GetModuleDirectory

using namespace dictproto;

DictClient::~DictClient()
{
    Close();
}

void DictClient::Close()
{
    if (m_pipe != INVALID_HANDLE_VALUE)
    {
        CloseHandle(m_pipe);
        m_pipe = INVALID_HANDLE_VALUE;
    }
}

void DictClient::SpawnService()
{
    std::wstring dir = GetModuleDirectory(); // the DLL's directory (has the exe)
    if (dir.empty())
        return;
    std::wstring exe = dir + L"canto-dictd.exe";

    std::wstring cmd = L"\"" + exe + L"\"";
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    if (CreateProcessW(exe.c_str(), cmdBuf.data(), nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

bool DictClient::Connect()
{
    std::wstring name = PipeName();
    HANDLE pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                              nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE)
        return false;

    DWORD mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr))
    {
        CloseHandle(pipe);
        return false;
    }
    m_pipe = pipe;

    // Push the desired dictionary so the service serves the right one. SETDICT
    // blocks until the (re)load finishes, so a subsequent RESOLVE is ready.
    if (m_haveDesired)
    {
        std::vector<char> req;
        PutU32(req, OP_SETDICT);
        PutWStr(req, m_path);
        PutOverrides(req, m_overrides);

        std::vector<char> resp;
        if (!WriteMessage(m_pipe, req) || !ReadMessage(m_pipe, resp))
        {
            Close();
            return false;
        }
    }
    return true;
}

bool DictClient::EnsureConnected()
{
    if (m_pipe != INVALID_HANDLE_VALUE)
        return true;
    if (Connect())
        return true;

    // No service yet: start it and retry connecting while it comes up + loads.
    SpawnService();
    for (int i = 0; i < 40; ++i) // up to ~4s
    {
        if (Connect())
            return true;
        Sleep(100);
    }
    return false;
}

bool DictClient::Transact(const std::vector<char>& req, std::vector<char>& resp)
{
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        if (!EnsureConnected())
            return false;
        if (WriteMessage(m_pipe, req) && ReadMessage(m_pipe, resp))
            return true;
        Close(); // broken pipe: drop it and retry once (service may have died)
    }
    return false;
}

void DictClient::SetDictionary(const std::wstring& path,
                               const std::vector<Override>& overrides)
{
    m_path = path;
    m_overrides = overrides;
    m_haveDesired = true;

    // Force a fresh connection so the new choice is pushed (SETDICT on connect),
    // and proactively bring the service up + loading now rather than on the first
    // keystroke.
    Close();
    EnsureConnected();
}

std::vector<EngineCandidate> DictClient::Resolve(const std::wstring& romanization)
{
    std::vector<char> req;
    PutU32(req, OP_RESOLVE);
    PutWStr(req, romanization);

    std::vector<char> resp;
    if (Transact(req, resp))
    {
        Reader r(resp);
        unsigned status = r.u32();
        if (status == STATUS_OK)
        {
            std::vector<Candidate> pairs;
            if (GetCandidates(r, pairs))
            {
                std::vector<EngineCandidate> out;
                out.reserve(pairs.size());
                for (Candidate& p : pairs)
                    out.push_back({ std::move(p.first), std::move(p.second) });
                return out;
            }
        }
    }
    return ResolveFallback(romanization);
}

std::vector<EngineCandidate> DictClient::ResolveFallback(const std::wstring& rom)
{
    if (!m_haveDesired || m_path.empty())
        return {};

    if (!m_fbLoaded || m_fbPath != m_path || m_fbOverrides != m_overrides)
    {
        m_fallback.Load(m_path);
        m_fallback.ApplyOverrides(m_overrides);
        m_fbPath = m_path;
        m_fbOverrides = m_overrides;
        m_fbLoaded = true;
    }
    return BuildCandidates(m_fallback, rom);
}
