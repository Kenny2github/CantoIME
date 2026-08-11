// CandidateEngine.h
//
// The pure romanization -> candidate logic, factored out of the text service so
// it can run wherever the dictionary lives: in the dictionary service process
// (canto-dictd) that owns the loaded dictionary, and in the in-process fallback
// the IME uses if the service is unavailable. Depends only on a Dictionary and
// the typed romanization buffer; no TSF or UI state.

#pragma once

#include <string>
#include <vector>

class Dictionary;

// One candidate. `display` is what the candidate window shows (may include a
// trailing romanization remainder, e.g. L"高o"); `commit` is the Chinese text
// inserted when it is accepted (L"高").
struct EngineCandidate
{
    std::wstring display;
    std::wstring commit;
};

// Builds the ordered, de-duplicated candidate list for a romanization buffer
// against the given dictionary. Empty buffer -> empty result.
std::vector<EngineCandidate> BuildCandidates(const Dictionary& dict,
                                             const std::wstring& romanization);
