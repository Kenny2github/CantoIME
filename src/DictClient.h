// DictClient.h
//
// The IME side of the dictionary service. Connects to canto-dictd over the
// per-user named pipe, tells it which dictionary to serve, and resolves
// romanization buffers to candidates. If the service is unreachable it spawns
// it; if it still cannot be reached, it falls back to loading the dictionary
// in-process so the IME keeps working (just without the shared-load speedup).

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <vector>
#include <utility>

#include "CandidateEngine.h"   // EngineCandidate
#include "Dictionary.h"        // in-process fallback

class DictClient
{
public:
    using Override = std::pair<std::wstring, std::vector<std::wstring>>;

    DictClient() = default;
    ~DictClient();

    DictClient(const DictClient&) = delete;
    DictClient& operator=(const DictClient&) = delete;

    // Selects the dictionary + overrides to serve (idempotent). Spawns the
    // service if needed and pushes the choice to it. Best-effort.
    void SetDictionary(const std::wstring& path, const std::vector<Override>& overrides);

    // Resolves a romanization buffer to candidates via the service, falling back
    // to an in-process dictionary on any failure.
    std::vector<EngineCandidate> Resolve(const std::wstring& romanization);

private:
    HANDLE                m_pipe = INVALID_HANDLE_VALUE;
    std::wstring          m_path;        // desired dictionary path
    std::vector<Override> m_overrides;   // desired overrides
    bool                  m_haveDesired = false;

    // In-process fallback, loaded lazily only when the service is unavailable.
    Dictionary            m_fallback;
    std::wstring          m_fbPath;
    std::vector<Override> m_fbOverrides;
    bool                  m_fbLoaded = false;

    void Close();
    bool Connect();          // open + configure the pipe, then send SETDICT
    bool EnsureConnected();  // connect, spawning the service if the pipe is absent
    void SpawnService();
    bool Transact(const std::vector<char>& req, std::vector<char>& resp);
    std::vector<EngineCandidate> ResolveFallback(const std::wstring& rom);
};
