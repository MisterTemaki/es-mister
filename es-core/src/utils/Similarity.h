#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace Utils { namespace String {
// Preserve the existing relevancy scores while sizing match maps to the actual strings.
inline float jaroWinkler(std::string a, std::string b, bool caseSensitive = true)
{
    if (a.empty() || b.empty()) return 0;
    if (!caseSensitive)
    {
        auto lower = [](unsigned char c) { return (char)std::tolower(c); };
        std::transform(a.begin(), a.end(), a.begin(), lower);
        std::transform(b.begin(), b.end(), b.begin(), lower);
    }
    if (a == b) return 1;
    const size_t n = a.size(), m = b.size();
    const size_t half = std::max(n, m) / 2, range = half ? half - 1 : 0;
    std::vector<uint8_t> matchedA(n), matchedB(m);
    size_t matches = 0;
    auto lowAt = [range](size_t i) { return i > range ? i - range : 0; };
    auto endAt = [range, m](size_t i) { return i < m && range < m - i - 1 ? i + range + 1 : m; };
    if (n <= 1024 && m <= 1024 && n + m <= 1024)
    {
        for (size_t i = 0; i < n; ++i)
            for (size_t j = lowAt(i), end = endAt(i); j < end; ++j)
                if (!matchedB[j] && a[i] == b[j])
                { matchedA[i] = matchedB[j] = 1; ++matches; break; }
    }
    else
    {
        // The same greedy first-available match, using per-byte lists for long metadata.
        // Each index is consumed or discarded once, avoiding quadratic scans of long strings.
        std::array<size_t, 256> first;
        first.fill(m);
        std::vector<size_t> next(m);
        for (size_t j = m; j-- > 0;)
        { auto c = (unsigned char)b[j]; next[j] = first[c]; first[c] = j; }
        for (size_t i = 0; i < n; ++i)
        {
            auto& j = first[(unsigned char)a[i]];
            const size_t low = lowAt(i);
            while (j < m && j < low) j = next[j];
            if (j < endAt(i))
            { matchedA[i] = matchedB[j] = 1; ++matches; j = next[j]; }
        }
    }
    if (!matches) return 0;
    size_t transitions = 0, j = 0;
    for (size_t i = 0; i < n; ++i)
        if (matchedA[i])
        {
            while (j < m && !matchedB[j]) ++j;
            if (j < m && a[i] != b[j]) ++transitions;
            if (j < m) ++j;
        }
    const float f = (float)matches;
    // Keep integer division of transitions: changing it would alter existing search ordering.
    float weight = (f / n + f / m + (f - (transitions / 2)) / f) / 3;
    if (weight > 0.7)
    {
        size_t prefix = 0;
        while (prefix < 4 && prefix < n && prefix < m && a[prefix] == b[prefix]) ++prefix;
        weight += prefix * 0.1f * (1 - weight);
    }
    return weight;
}
} }
