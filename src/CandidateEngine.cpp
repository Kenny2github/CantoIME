// CandidateEngine.cpp
//
// Extracted verbatim from the former CTextService::BuildCandidates. See the long
// comment below for the segmentation/ordering rules.

#include "CandidateEngine.h"
#include "Dictionary.h"

#include <unordered_set>
#include <functional>
#include <algorithm>

static const size_t kMaxCandidates = 300;
static const size_t kMaxPrefixKeys = 200;

// The buffer is split into a run of leading exact keys (each locked to its best
// character) followed by a non-empty final segment that is prefix-matched. Every
// such split is enumerated, then ordered by its per-key consumption sequence
// (leading key lengths, then the final segment's length) compared largest-first.
// So for "abcdefg" with keys abc/def/g/abcd/efg, "abcd efg" (lengths 4,3) beats
// "abc def g" (lengths 3,3,1) because its first key is longer, not because it
// uses fewer keys. The 0-leading-key split is the whole buffer prefix-matched:
// its first key consumes all n characters, the maximum possible, so a real key
// spanning the buffer still ranks first, e.g. "hah" surfaces 哈哈 (key "haha")
// before 下下 (a doubling of "ha").
//
// Each split emits one entry per character of each prefix-matched key, displayed
// as <locked chars><char><leftover romanization> and committed as <locked chars>
// <char>. All splits share one dedup set keyed on the commit string, so the first
// (greediest) segmentation to produce a given Chinese text wins and later
// duplicates drop.
std::vector<EngineCandidate> BuildCandidates(const Dictionary& dict,
                                             const std::wstring& romanization)
{
    std::vector<EngineCandidate> candidates;
    if (romanization.empty())
        return candidates;

    const std::wstring& buf = romanization;
    const size_t n = buf.size();

    std::unordered_set<std::wstring> seenCommits;

    // Emits candidates for one (locked prefix, open final segment) split.
    // Returns false once the candidate cap is reached so callers stop early.
    auto emit = [&](const std::wstring& locked, const std::wstring& finalSeg) -> bool
    {
        std::vector<std::wstring> keys = dict.KeysWithPrefix(finalSeg, kMaxPrefixKeys);
        for (const std::wstring& key : keys)
        {
            std::wstring remainder = key.substr(finalSeg.size()); // "" for exact match
            for (const std::wstring& ch : dict.Lookup(key))
            {
                std::wstring commit = locked + ch;
                if (!seenCommits.insert(commit).second)
                    continue; // already emitted this text via a greedier split
                EngineCandidate c;
                c.display = commit + remainder;
                c.commit  = std::move(commit);
                candidates.push_back(std::move(c));
                if (candidates.size() >= kMaxCandidates)
                    return false;
            }
        }
        return true;
    };

    // ---- Enumerate every split: partition a prefix [0, consumed) of the buffer
    // into leading exact keys (locked to their best character), leaving a
    // non-empty final segment [consumed, n) to prefix-match. `seq` is the split's
    // consumption profile: the leading key lengths followed by the final segment
    // length (always summing to n). ----
    struct Split { std::wstring locked; size_t consumed; std::vector<size_t> seq; };
    std::vector<Split> splits;
    const size_t kMaxSplits = 256; // safety valve against pathological fan-out

    std::function<void(size_t, const std::wstring&, const std::vector<size_t>&)> gather =
        [&](size_t pos, const std::wstring& locked, const std::vector<size_t>& lens)
    {
        if (splits.size() >= kMaxSplits)
            return;
        std::vector<size_t> seq = lens;
        seq.push_back(n - pos);                       // the final segment's length
        splits.push_back({ locked, pos, std::move(seq) }); // pos < n

        // Extend by one more leading exact key, keeping the final segment
        // non-empty (pos + len < n). Lengths are bounded by the longest exact
        // key at this position.
        size_t maxLen = dict.LongestExactPrefixLen(buf, pos);
        for (size_t len = 1; len <= maxLen && pos + len < n; ++len)
        {
            std::wstring key = buf.substr(pos, len);
            const auto& chars = dict.Lookup(key);
            if (chars.empty())
                continue; // not an exact key at this length
            std::vector<size_t> nextLens = lens;
            nextLens.push_back(len);
            gather(pos + len, locked + chars.front(), nextLens);
        }
    };
    gather(0, std::wstring(), std::vector<size_t>());

    // Greediest consumption first: compare the length sequences element by
    // element, larger length wins. stable_sort keeps discovery order on ties.
    std::stable_sort(splits.begin(), splits.end(),
        [](const Split& a, const Split& b)
        {
            size_t m = std::min(a.seq.size(), b.seq.size());
            for (size_t i = 0; i < m; ++i)
                if (a.seq[i] != b.seq[i])
                    return a.seq[i] > b.seq[i];
            return a.seq.size() < b.seq.size();
        });

    for (const Split& s : splits)
        if (!emit(s.locked, buf.substr(s.consumed)))
            break;

    return candidates;
}
