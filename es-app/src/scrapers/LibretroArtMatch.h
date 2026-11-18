// Which libretro-thumbnails art is a game's: its name and every art name reduced to the same words, then compared.
//
// From Snes9x PS5's fe_artmatch.h and fe_titlematch.h (https://github.com/MisterTemaki/snes9xPS5, MIT), as they are
// but for two changes: the region codes MiSTer packs use ("(JP)", "(US)", "(EU)", "(US, EU)") are read like No-Intro's
// regions, and names come without their extension (ES gives the file's stem).
//
//   * (...) [...] {...} tags, "# SNES" labels, trailing "English" / "v1.1" go;
//   * roman numerals become digits, "Dai-4-ji" becomes 4, articles and "no" go;
//   * Japanese romanisation variants collapse (Butouden = Butoden, Shin = Sin) and a few translation pairs are one
//     word (Taisen = Wars);
// and Resolve compares them: the same words in the same order, else a weighted word-set match that must be complete,
// match every number, and beat the next game clearly. Anything doubtful returns "": a missing cover is better than
// another game's.
//
// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace LibretroArtMatch {
// Bounded Levenshtein distance with early exit. O(k*length), k<=3.
inline int Distance(const std::string& a, const std::string& b, int limit)
{
    if (a == b) return 0;
    if (a.empty() || b.empty() || a.size() > b.size() + size_t(limit) ||
        b.size() > a.size() + size_t(limit)) return limit + 1;
    std::vector<int> prev(b.size()+1), curr(b.size()+1);
    for (size_t j=0;j<=b.size();++j) prev[j]=int(j);
    for (size_t i=1;i<=a.size();++i)
    {
        curr[0]=int(i);
        int min_row=curr[0];
        for (size_t j=1;j<=b.size();++j)
        {
            curr[j]=std::min({prev[j]+1, curr[j-1]+1,
                              prev[j-1] + (a[i-1]==b[j-1] ? 0 : 1)});
            min_row=std::min(min_row,curr[j]);
        }
        if (min_row>limit) return limit+1;
        prev.swap(curr);
    }
    return prev[b.size()];
}

inline std::string Lower(std::string s)
{
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
// What a name says besides its title.
struct Parsed
{
    std::string title;       // outside the tags, without version and language words, the author's spelling
    std::string region;      // "USA", "Japan", "Europe", "World", or ""
    bool translated = false; // marked as a translation or patch
    int penalty = 0;         // 0 for a plain release; Beta, Proto, Sample... 100, Rev 1, a [T-En] tag 5
};
inline void ClassifyTag(std::string tag, Parsed& out)
{
    const std::string t = Lower(tag);
    auto has = [&t](const char* w) { return t.find(w) != std::string::npos; };
    if (out.region.empty())
    {
        static const char* const regions[][2] = {{"usa", "USA"}, {"japan", "Japan"}, {"europe", "Europe"}, {"world", "World"}};
        size_t first = std::string::npos;
        for (const auto& r : regions)
        {
            const size_t at = t.find(r[0]);
            if (at < first) { first = at; out.region = r[1]; }
        }
        // GoodTools codes: (U) (J) (E) (UE) (JU)
        if (out.region.empty() && !t.empty() && t.size() <= 2 && t.find_first_not_of("uje") == std::string::npos)
            out.region = t[0] == 'u' ? "USA" : t[0] == 'j' ? "Japan" : "Europe";
        // two- and three-letter codes, alone or in a list: (JP) (US) (EU) (US, EU) (JP - US) (W)
        if (out.region.empty())
        {
            static const char* const codes[][2] = {{"us", "USA"}, {"jp", "Japan"}, {"ja", "Japan"}, {"jpn", "Japan"},
                {"eu", "Europe"}, {"eur", "Europe"}, {"w", "World"}, {"wr", "World"}, {"wor", "World"}};
            size_t from = 0;
            while (out.region.empty() && from <= t.size())
            {
                size_t to = t.find_first_of(",-/", from);
                if (to == std::string::npos) to = t.size();
                std::string piece = t.substr(from, to - from);
                while (!piece.empty() && piece.front() == ' ') piece.erase(piece.begin());
                while (!piece.empty() && piece.back() == ' ') piece.pop_back();
                for (const auto& c : codes)
                    if (piece == c[0]) { out.region = c[1]; break; }
                from = to + 1;
            }
        }
    }
    if (t.compare(0, 2, "t+") == 0 || t.compare(0, 2, "t-") == 0 || has("eng") || has("transl") || has("patch") ||
        has("fan trans") || has("fantrans"))
    {
        out.translated = true;
        out.penalty += 5;
    }
    for (const char* bad : {"beta", "proto", "sample", "demo", "unl", "pirate", "kiosk", "virtual console", "aftermarket",
                            "hack", "collection", "arcade", "not for resale", "bootleg"})
        if (has(bad)) { out.penalty += 100; break; }
    // MAME's lists: a game's Korean release after its own ("Quiz Daisousa Sen (NGM-023)", not "(Korean release)")
    if (has("korean")) out.penalty += 3;
    if (t.compare(0, 3, "rev") == 0) out.penalty += 1;
}
// "v1.1", "ver 1.2", "1.02": a version, not a title word.
inline bool IsVersion(const std::string& w)
{
    if (w.size() > 1 && w[0] == 'v' && std::isdigit(static_cast<unsigned char>(w[1]))) return true;
    if (w == "ver" || w == "version") return true;
    if (w.compare(0, 3, "ver") == 0 && w.size() > 3 && std::isdigit(static_cast<unsigned char>(w[3]))) return true;
    if (w.compare(0, 3, "rev") == 0 && (w.size() == 3 || std::isdigit(static_cast<unsigned char>(w[3])))) return true;
    return w.find('.') != std::string::npos && w.find_first_not_of("0123456789.") == std::string::npos;
}
inline Parsed Parse(const std::string& name)
{
    Parsed out;
    std::string s = name;
    // a frontend label: "Chrono Trigger (ENG) # SNES"
    const size_t hash = s.rfind('#');
    if (hash != std::string::npos)
    {
        size_t letters = 0;
        bool label = true;
        for (size_t i = hash + 1; i < s.size(); ++i)
        {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            if (std::isalpha(c)) ++letters;
            else if (!std::isspace(c)) label = false;
        }
        if (label && letters >= 2) s.resize(hash);
    }
    std::string text, tag;
    int depth = 0;
    for (char c : s)
    {
        if (c == '(' || c == '[' || c == '{') { if (depth++ > 0) tag += c; else tag.clear(); }
        else if ((c == ')' || c == ']' || c == '}') && depth > 0)
        {
            if (--depth == 0) { ClassifyTag(tag, out); text += ' '; }
            else tag += c;
        }
        else if (depth > 0) tag += c;
        else text += (c == '_') ? ' ' : c;
    }
    if (depth > 0) ClassifyTag(tag, out);
    std::vector<std::string> words;
    {
        std::string w;
        for (char c : text)
        {
            if (std::isspace(static_cast<unsigned char>(c))) { if (!w.empty()) words.push_back(w); w.clear(); }
            else w += c;
        }
        if (!w.empty()) words.push_back(w);
    }
    // trailing language and version words: "Super Robot Wars 4 English v1.1"
    while (!words.empty())
    {
        std::string w = Lower(words.back());
        while (!w.empty() && !std::isalnum(static_cast<unsigned char>(w.back()))) w.pop_back();
        size_t b = 0;
        while (b < w.size() && !std::isalnum(static_cast<unsigned char>(w[b]))) ++b;
        w = w.substr(b);
        if (w.empty()) { words.pop_back(); continue; } // a dangling "-"
        if (w == "english" || w == "eng" || w == "translated" || w == "translation" || w == "patched" || w == "patch" ||
            w == "fantrans" || w == "fantranslation" || w == "fan" || w == "subbed")
        {
            out.translated = true;
            words.pop_back();
            continue;
        }
        // a translation mark that is not in brackets: "Super Robot Wars 4 T-Eng (2026)(Aeon Genesis)"
        if (w.size() >= 4 && w[0] == 't' && (w[1] == '-' || w[1] == '+'))
        {
            out.translated = true;
            words.pop_back();
            continue;
        }
        if (IsVersion(w)) { words.pop_back(); continue; }
        break;
    }
    // what a ROM hack adds after its game's title: "Megaman X3 - Fastrom v2.1", "Gradius III - SA-1",
    // "Final Fight 3 Optimized Fastrom" (the last part after " - " that has one of these words goes, or the words alone)
    auto hackWord = [](std::string w) {
        w = Lower(w);
        while (!w.empty() && !std::isalnum(static_cast<unsigned char>(w.back()))) w.pop_back();
        for (const char* h : {"fastrom", "sa-1", "sa1", "msu-1", "msu1", "msu", "optimized", "optimised", "hack", "fix",
                              "reduction", "translation", "translated", "english", "eng", "patch", "patched", "redux"})
            if (w == h) return true;
        return (w.size() >= 4 && w[0] == 't' && (w[1] == '+' || w[1] == '-')) || IsVersion(w);
    };
    // A part after " - " goes whole when it starts with such a word or is two words at most ("- Fastrom v1.1",
    // "- Camera Fix"); otherwise only such words at its end ("Contra III - The Alien Wars SA-1").
    for (bool cut = true; cut && words.size() > 1;)
    {
        cut = false;
        size_t dash = words.size();
        while (dash > 0 && words[dash - 1] != "-") --dash;
        bool any = false;
        for (size_t i = dash; i < words.size() && !any; ++i) any = hackWord(words[i]);
        if (dash > 1 && dash < words.size() && any && (hackWord(words[dash]) || words.size() - dash <= 2))
        {
            words.resize(dash - 1);
            out.translated = cut = true;
        }
        else if (hackWord(words.back()) && words.size() > 2)
        {
            words.pop_back();
            out.translated = cut = true;
        }
    }
    for (size_t i = 0; i < words.size(); ++i) out.title += (i ? " " : "") + words[i];
    return out;
}
// "Dragon Ball Z - Super Butouden" -> "Dragon Ball Z": the part before the subtitle.
inline std::string MainOf(const std::string& title)
{
    for (size_t i = 0; i < title.size(); ++i)
    {
        if (title[i] == ':') return title.substr(0, i);
        if (title[i] == '-' && i > 0 && i + 1 < title.size() && title[i - 1] == ' ' && title[i + 1] == ' ')
            return title.substr(0, i);
    }
    return title;
}
// "Dragon Ball Z - Super Butouden" -> "Super Butouden": the subtitle, or "" when there is none.
inline std::string TailOf(const std::string& title)
{
    const std::string head = MainOf(title);
    if (head.size() >= title.size()) return "";
    const size_t sep = title.find_first_not_of(" -:", head.size());
    return sep == std::string::npos ? "" : title.substr(sep);
}
// Japanese romanisation variants to one spelling: Hepburn to Nihon-shiki, long vowels to short.
inline std::string Phon(const std::string& in)
{
    static const char* const spell[][2] = {{"shi", "si"}, {"chi", "ti"}, {"tsu", "tu"}, {"sha", "sya"}, {"shu", "syu"},
        {"sho", "syo"}, {"cha", "tya"}, {"chu", "tyu"}, {"cho", "tyo"}, {"ja", "zya"}, {"ju", "zyu"}, {"jo", "zyo"},
        {"ji", "zi"}, {"fu", "hu"}};
    std::string t;
    for (size_t i = 0; i < in.size();)
    {
        bool done = false;
        for (const auto& m : spell)
        {
            const size_t n = std::strlen(m[0]);
            if (in.compare(i, n, m[0]) == 0) { t += m[1]; i += n; done = true; break; }
        }
        if (!done) t += in[i++];
    }
    std::string out;
    for (char c : t)
    {
        if (!out.empty())
        {
            const char p = out.back();
            if (p == c && std::strchr("aeiou", c)) continue; // oo uu aa ee ii
            if (p == 'o' && c == 'u') continue;              // ou
        }
        out += c;
    }
    return out;
}
// Words a translator renders differently from the Japanese title, as one word.
inline const std::string& Canon(const std::string& word, std::string& scratch)
{
    static const char* const pairs[][2] = {{"taisen", "wars"}, {"war", "wars"}, {"kidou", "mobile"}, {"suit", "warrior"},
        {"senshi", "warrior"}, {"warriors", "warrior"}, {"soldier", "warrior"}, {"soldiers", "warrior"},
        {"densetsu", "legend"}, {"legends", "legend"}, {"monogatari", "story"}, {"stories", "story"}, {"tale", "story"},
        {"tales", "story"}, {"yuusha", "hero"}, {"heroes", "hero"}, {"mahou", "magic"}, {"kyojin", "giant"},
        {"tatakai", "battle"}, {"battles", "battle"}};
    static const std::unordered_map<std::string, std::string> map = [] {
        std::unordered_map<std::string, std::string> m;
        for (const auto& p : pairs) m[Phon(p[0])] = Phon(p[1]);
        return m;
    }();
    scratch = Phon(word);
    const auto it = map.find(scratch);
    if (it != map.end()) scratch = it->second;
    return scratch;
}
// 2..20, or 0.
inline int Roman(const std::string& w)
{
    static const char* const numerals[] = {"ii", "iii", "iv", "", "vi", "vii", "viii", "ix", "", "xi", "xii", "xiii", "xiv",
        "xv", "xvi", "xvii", "xviii", "xix", "xx"};
    for (int i = 0; i < int(sizeof(numerals) / sizeof(numerals[0])); ++i)
        if (*numerals[i] && w == numerals[i]) return i + 2;
    return 0;
}
inline bool IsDigits(const std::string& w) { return !w.empty() && std::isdigit(static_cast<unsigned char>(w[0])); }
// The plain letter of an accented Latin one in UTF-8 (lead byte 0xC3..0xC5, then the second byte), or 0: the vowels with
// grave, acute, circumflex, tilde, diaeresis or macron (Japanese long vowels: "ō"), ñ and ç.
inline char PlainLetter(unsigned char lead, unsigned char next)
{
    if (lead == 0xC3 && next >= 0x80 && next <= 0xBF)
    {
        static const char latin1[] = "AAAAAAACEEEEIIII" "DNOOOOO*OUUUUYTs" "aaaaaaaceeeeiiii" "dnooooo/ouuuuyty";
        const char c = latin1[next - 0x80];
        return (c == '*' || c == '/') ? 0 : c;
    }
    if (lead == 0xC4) // Ā ā Ă ă Ą ą ... Ē ē ... Ī ī
    {
        if (next >= 0x80 && next <= 0x85) return (next & 1) ? 'a' : 'A';
        if (next >= 0x92 && next <= 0x9B) return (next & 1) ? 'e' : 'E';
        if (next >= 0xAA && next <= 0xB1) return (next & 1) ? 'i' : 'I';
        return 0;
    }
    if (lead == 0xC5) // Ō ō Ŏ ŏ Ő ő ... Ū ū Ŭ ŭ Ů ů Ű ű
    {
        if (next >= 0x8C && next <= 0x91) return (next & 1) ? 'o' : 'O';
        if (next >= 0xAA && next <= 0xB3) return (next & 1) ? 'u' : 'U';
        return 0;
    }
    return 0;
}
// The comparable words of a title. Order is kept.
inline std::vector<std::string> Tokens(const std::string& title)
{
    struct Word { std::string text; };
    std::vector<Word> words;
    std::string cur;
    auto flush = [&]() {
        if (!cur.empty()) words.push_back({Lower(cur)});
        cur.clear();
    };
    for (size_t i = 0; i < title.size(); ++i)
    {
        unsigned char c = static_cast<unsigned char>(title[i]);
        // an accented Latin letter (UTF-8) as its plain one: "Daisōsasen" -> daisosasen, "Pokémon" -> pokemon
        if ((c == 0xC3 || c == 0xC4 || c == 0xC5) && i + 1 < title.size())
        {
            const char plain = PlainLetter(c, static_cast<unsigned char>(title[i + 1]));
            if (plain != 0) { c = static_cast<unsigned char>(plain); ++i; }
        }
        if (c == '\'') continue; // Yoshi's -> yoshis
        if (c == 0xE2 && i + 2 < title.size() && static_cast<unsigned char>(title[i + 1]) == 0x80 &&
            static_cast<unsigned char>(title[i + 2]) == 0x99) { i += 2; continue; } // the same, curly
        if (std::isalnum(c))
        {
            // Taisen4 -> taisen 4
            if (!cur.empty() && (std::isdigit(c) != 0) != (std::isdigit(static_cast<unsigned char>(cur.back())) != 0)) flush();
            cur += char(c);
        }
        else flush();
    }
    flush();
    std::vector<std::string> raw;
    for (size_t i = 0; i < words.size(); ++i)
    {
        std::string w = words[i].text;
        if (IsDigits(w))
        {
            const size_t nz = w.find_first_not_of('0');
            raw.push_back(nz == std::string::npos ? "0" : w.substr(nz));
            continue;
        }
        int n = Roman(w);
        // a lone I or V after the first word, in any case: "Dragon Quest I", "final fantasy v"
        if (!n && (w == "i" || w == "v") && i > 0) n = w == "i" ? 1 : 5;
        raw.push_back(n ? std::to_string(n) : w);
    }
    std::vector<std::string> out;
    static const char* const stop[] = {"the", "a", "an", "of", "and", "no", "in", "at", "for", "to"};
    std::string scratch;
    for (size_t i = 0; i < raw.size(); ++i)
    {
        const std::string& w = raw[i];
        if (w == "dai" && i + 1 < raw.size() && IsDigits(raw[i + 1])) // Dai-4-ji
        {
            out.push_back(raw[i + 1]);
            i += (i + 2 < raw.size() && (raw[i + 2] == "ji" || raw[i + 2] == "zi")) ? 2 : 1;
            continue;
        }
        if (IsDigits(w)) { out.push_back(w); continue; }
        bool stopped = false;
        for (const char* s : stop) if (w == s) { stopped = true; break; }
        if (!stopped) out.push_back(Canon(w, scratch));
    }
    return out;
}
// A title's letters and digits as one string, romanisation variants collapsed: what a ROM header can be compared with.
inline std::string RawKey(const std::string& s)
{
    std::string letters;
    for (char c : s)
        if (std::isalnum(static_cast<unsigned char>(c))) letters += char(std::tolower(static_cast<unsigned char>(c)));
    return Phon(letters);
}
// The words of a name joined: two names of one game (any region, revision, tag) have the same key.
inline std::string TitleKey(const std::string& name)
{
    std::string key;
    for (const std::string& w : Tokens(Parse(name).title)) key += w;
    return key;
}

// True when both names have numbers and neither's numbers are inside the other's: "Dragon Quest III" is not
// "Dragon Quest I & II" (as letters they are the same), "Final Fantasy III" is not "Final Fantasy II". A number
// only one side has ("Fire Emblem 5 - Thracia 776" for "Fire Emblem - Thracia 776") is not a conflict.
inline bool NumbersConflict(const std::string& a, const std::string& b)
{
    auto numbers = [](const std::string& name) {
        std::vector<std::string> n;
        for (const std::string& w : Tokens(Parse(name).title))
            if (IsDigits(w)) n.push_back(w);
        std::sort(n.begin(), n.end());
        return n;
    };
    const std::vector<std::string> x = numbers(a), y = numbers(b);
    if (x.empty() || y.empty()) return false;
    return !std::includes(x.begin(), x.end(), y.begin(), y.end()) && !std::includes(y.begin(), y.end(), x.begin(), x.end());
}

struct Entry
{
    int pos = 0;                        // in the names list
    Parsed p;
    std::vector<std::string> all, head, tail; // the words of the title, of its main part before " - ", and of what follows
    std::string joined;                 // all, joined: the same for every region and revision of a game
};
// An art list's names worked out once (like titles::Index).
struct Index
{
    const std::vector<std::string>* names = nullptr;
    size_t count = 0;
    std::vector<Entry> entries;
    std::unordered_map<std::string, std::vector<int>> by_joined; // a game -> its entries, in list order
    std::unordered_map<std::string, std::vector<int>> by_word;   // a word -> the first entry of each game that has it
    std::unordered_map<std::string, int> games_with;             // a word -> how many games have it
    std::vector<std::string> vocabulary;
    std::vector<std::pair<std::string, int>> by_raw;             // sorted RawKey -> entry, for headers
    std::vector<std::pair<std::string, int>> by_key;             // sorted joined words -> entry, for headers
    // MAME's lists (the arcade, the Neo Geo) give a game's other titles after " / " ("Art of Fighting / Ryuuko no Ken"),
    // " _ " in an art name: each such title, joined -> the entries that have it (by_first: the first title only)
    std::unordered_map<std::string, std::vector<int>> by_alternate, by_first;
    double games = 1;
};
// The titles of an art name that " _ " parts ("Art of Fighting _ Ryuuko no Ken (NGM-044)" -> "Art of Fighting",
// "Ryuuko no Ken"): outside brackets, each part with its own words; none when there is no " _ ".
inline std::vector<std::string> AlternateTitles(const std::string& name)
{
    std::vector<std::string> parts;
    std::string text;
    int depth = 0;
    for (char c : name)
    {
        if (c == '(' || c == '[' || c == '{') ++depth;
        else if ((c == ')' || c == ']' || c == '}') && depth > 0) --depth;
        else if (depth == 0) text += c;
    }
    size_t from = 0;
    for (;;)
    {
        const size_t at = text.find(" _ ", from);
        parts.push_back(text.substr(from, at == std::string::npos ? std::string::npos : at - from));
        if (at == std::string::npos) break;
        from = at + 3;
    }
    if (parts.size() < 2) parts.clear();
    return parts;
}
inline void Build(Index& ix, const std::vector<std::string>& names)
{
    ix = Index();
    ix.names = &names;
    ix.count = names.size();
    ix.entries.reserve(names.size());
    for (size_t i = 0; i < names.size(); ++i)
    {
        Entry e;
        e.pos = int(i);
        e.p = Parse(names[i]);
        e.all = Tokens(e.p.title);
        if (e.all.empty()) continue;
        e.head = Tokens(MainOf(e.p.title));
        if (e.head.empty()) e.head = e.all;
        e.tail = Tokens(TailOf(e.p.title));
        for (const std::string& w : e.all) e.joined += w;
        const std::vector<std::string> titles = AlternateTitles(names[i]);
        for (size_t t = 0; t < titles.size(); ++t)
        {
            std::string key;
            for (const std::string& w : Tokens(Parse(titles[t]).title)) key += w;
            if (key.empty() || key == e.joined) continue;
            std::vector<int>& alternate = ix.by_alternate[key];
            if (alternate.empty() || alternate.back() != int(ix.entries.size())) alternate.push_back(int(ix.entries.size()));
            if (t == 0) ix.by_first[key].push_back(int(ix.entries.size()));
        }
        ix.entries.push_back(std::move(e));
    }
    for (size_t k = 0; k < ix.entries.size(); ++k)
    {
        const Entry& e = ix.entries[k];
        std::vector<int>& group = ix.by_joined[e.joined];
        if (group.empty())
        {
            std::vector<std::string> unique = e.all;
            std::sort(unique.begin(), unique.end());
            unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
            for (const std::string& w : unique)
            {
                ++ix.games_with[w];
                ix.by_word[w].push_back(int(k));
            }
        }
        group.push_back(int(k));
        const std::string& title = e.p.title;
        ix.by_key.emplace_back(e.joined, int(k));
        ix.by_raw.emplace_back(RawKey(title), int(k));
        // "Legend of Zelda, The - A Link to the Past" is also "The Legend of Zelda - A Link to the Past"
        for (const char* article : {", The", ", A", ", An"})
        {
            const size_t at = title.find(article);
            const size_t n = std::strlen(article);
            if (at != std::string::npos && (at + n == title.size() || title[at + n] == ' '))
                ix.by_raw.emplace_back(RawKey(std::string(article + 2) + " " + title.substr(0, at) + title.substr(at + n)), int(k));
        }
    }
    ix.games = double(ix.by_joined.size() ? ix.by_joined.size() : 1);
    for (const auto& w : ix.games_with) ix.vocabulary.push_back(w.first);
    std::sort(ix.vocabulary.begin(), ix.vocabulary.end());
    std::sort(ix.by_raw.begin(), ix.by_raw.end());
    std::sort(ix.by_key.begin(), ix.by_key.end());
}
// The index of a list that lives as long as the program (the built-in tables), made on first use.
inline const Index& IndexOf(const std::vector<std::string>& names)
{
    static std::mutex lock;
    static std::map<const std::vector<std::string>*, std::unique_ptr<Index>> made;
    std::lock_guard<std::mutex> hold(lock);
    std::unique_ptr<Index>& ix = made[&names];
    if (!ix || ix->count != names.size())
    {
        ix.reset(new Index);
        Build(*ix, names);
    }
    return *ix;
}
// A rare word says more than a common one; a number counts as a rare word; a word nobody has counts most.
inline double Weight(const Index& ix, const std::string& w)
{
    if (IsDigits(w)) return 3.0;
    const auto it = ix.games_with.find(w);
    const double n = it == ix.games_with.end() ? 0.0 : double(it->second);
    return std::max(0.3, std::log((ix.games + 1.0) / (n + 0.5)));
}
// How well two lists of words describe the same title, 0..1. Every number on either side must be matched.
inline double Fit(const Index& ix, const std::vector<std::string>& q, const std::vector<std::string>& c)
{
    std::vector<char> used(c.size(), 0), hit(q.size(), 0);
    double hit_weight = 0;
    for (size_t i = 0; i < q.size(); ++i)
        for (size_t j = 0; j < c.size(); ++j)
            if (!used[j] && c[j] == q[i]) { used[j] = hit[i] = 1; hit_weight += Weight(ix, c[j]); break; }
    // a word of five letters or more may be one slip off (two from nine letters): a typo or a spelling
    for (size_t i = 0; i < q.size(); ++i)
    {
        if (hit[i] || IsDigits(q[i]) || q[i].size() < 5) continue;
        const int limit = q[i].size() >= 9 ? 2 : 1;
        int best = -1, best_d = limit + 1;
        for (size_t j = 0; j < c.size(); ++j)
        {
            if (used[j] || IsDigits(c[j])) continue;
            const int d = Distance(q[i], c[j], limit);
            if (d < best_d) { best_d = d; best = int(j); }
        }
        if (best >= 0) { used[size_t(best)] = hit[i] = 1; hit_weight += Weight(ix, c[size_t(best)]); }
    }
    double q_total = hit_weight, c_total = hit_weight;
    for (size_t i = 0; i < q.size(); ++i)
        if (!hit[i])
        {
            if (IsDigits(q[i])) return 0;
            q_total += Weight(ix, q[i]);
        }
    for (size_t j = 0; j < c.size(); ++j)
        if (!used[j])
        {
            if (IsDigits(c[j])) return 0;
            c_total += Weight(ix, c[j]);
        }
    if (hit_weight <= 0) return 0;
    const double cover_q = hit_weight / q_total, cover_c = hit_weight / c_total;
    return 2 * cover_q * cover_c / (cover_q + cover_c);
}
inline double Mass(const Index& ix, const std::vector<std::string>& words)
{
    double m = 0;
    for (const std::string& w : words) m += Weight(ix, w);
    return m;
}
// The same numbers on both sides: "Hard Battle II - ..." is not "Hard Battle".
inline bool SameNumbers(const std::vector<std::string>& a, const std::vector<std::string>& b)
{
    auto numbers = [](const std::vector<std::string>& words) {
        std::vector<std::string> n;
        for (const std::string& w : words)
            if (IsDigits(w)) n.push_back(w);
        std::sort(n.begin(), n.end());
        return n;
    };
    return numbers(a) == numbers(b);
}
// How well a query (its words, and the words before its subtitle) describes a game, 0..1. A translator keeps the main
// title and renames the rest ("Flying Hero - Bugyuru's Adventure" for "Flying Hero - Bugyuru no Daibouken"), or a
// retranslation drops the Japanese prefix: those count 0.9, and only with enough rare words to mean something.
inline double Score(const Index& ix, const std::vector<std::string>& all, const std::vector<std::string>& head, const Entry& e)
{
    double f = std::max(Fit(ix, all, e.all), Fit(ix, all, e.head));
    if (f >= 0.85 || !SameNumbers(all, e.all)) return f;
    if (head.size() != all.size() && Mass(ix, head) >= 4.0) f = std::max(f, 0.9 * Fit(ix, head, e.head));
    if (e.tail.size() >= 2 && Mass(ix, e.tail) >= 4.0) f = std::max(f, 0.9 * Fit(ix, all, e.tail));
    return f;
}
// Among one game's entries: the wanted region, else USA, World, Europe, Japan; a plain release before a Beta or a Rev;
// of equals, the shortest name, the one with the fewest other tags ("Aladdin (USA)", not "Aladdin (USA) (Final Cut)").
inline std::string Pick(const Index& ix, const std::vector<int>& group, const std::string& region)
{
    static const char* const order[] = {"USA", "World", "Europe", "Japan"};
    int best = -1, best_cost = INT_MAX;
    size_t best_length = 0;
    for (int k : group)
    {
        const Entry& e = ix.entries[size_t(k)];
        int rank = 4;
        for (int i = 0; i < 4; ++i)
            if (e.p.region == order[i]) rank = i;
        if (!region.empty() && e.p.region == region) rank = -1;
        const int cost = rank * 10 + e.p.penalty;
        const size_t length = (*ix.names)[size_t(e.pos)].size();
        if (cost < best_cost || (cost == best_cost && length < best_length)) { best_cost = cost; best = k; best_length = length; }
    }
    return (*ix.names)[size_t(ix.entries[size_t(best)].pos)];
}
// Of the games that tie for the best fit, the USA one when the tie is exactly two games with one main title, one released in the
// USA and one not, else -1. A retranslation of a game that came out in the USA ("Breath of Fire II - Retranslation") fits its
// USA entry and its Japanese one ("Breath of Fire II - Shimei no Ko") alike; the USA one is the answer wanted. Any other tie (a
// franchise of unrelated games that happen to have one USA release) is not told apart.
inline int UsaOfTwo(const Index& ix, const std::vector<std::pair<int, double>>& scored, double best_fit)
{
    int games[2] = {-1, -1};
    for (const auto& s : scored)
    {
        if (best_fit - s.second >= 0.05) continue;
        const std::string& joined = ix.entries[size_t(s.first)].joined;
        if (games[0] >= 0 && joined == ix.entries[size_t(games[0])].joined) continue;
        if (games[1] >= 0 && joined == ix.entries[size_t(games[1])].joined) continue;
        if (games[0] < 0) games[0] = s.first;
        else if (games[1] < 0) games[1] = s.first;
        else return -1; // a third game
    }
    if (games[1] < 0 || ix.entries[size_t(games[0])].head != ix.entries[size_t(games[1])].head) return -1;
    int usa[2] = {-1, -1};
    for (int i = 0; i < 2; ++i)
        for (int k : ix.by_joined.at(ix.entries[size_t(games[i])].joined))
            if (ix.entries[size_t(k)].p.region == "USA") usa[i] = k;
    if ((usa[0] >= 0) == (usa[1] >= 0)) return -1;
    return usa[0] >= 0 ? games[0] : games[1];
}
// Resolve's search by words (ResolveTitle): the query's words against the games that share its rarer ones.
inline std::string ResolveTitle(const Index& ix, const Parsed& q, const std::vector<std::string>& words, bool usa_on_tie,
                                bool whole_title)
{
    if (words.size() < 2) return "";
    // the games that share (or nearly share) one of the query's rarer words
    std::vector<std::string> rare;
    for (const std::string& w : words)
        if (!IsDigits(w) && std::find(rare.begin(), rare.end(), w) == rare.end()) rare.push_back(w);
    std::sort(rare.begin(), rare.end(), [&ix](const std::string& a, const std::string& b) { return Weight(ix, a) > Weight(ix, b); });
    if (rare.size() > 3) rare.resize(3);
    std::vector<int> candidates;
    for (const std::string& w : rare)
    {
        const auto exact = ix.by_word.find(w);
        if (exact != ix.by_word.end()) candidates.insert(candidates.end(), exact->second.begin(), exact->second.end());
        if (w.size() < 5) continue;
        const int limit = w.size() >= 9 ? 2 : 1;
        for (const std::string& v : ix.vocabulary)
            if (v != w && Distance(w, v, limit) <= limit)
            {
                const auto fuzzy = ix.by_word.find(v);
                candidates.insert(candidates.end(), fuzzy->second.begin(), fuzzy->second.end());
            }
    }
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    const std::vector<std::string> head = Tokens(MainOf(q.title));
    int best = -1, whole = -1;
    double best_fit = 0, second_fit = 0, whole_fit = 0, second_whole = 0;
    std::vector<std::pair<int, double>> scored;
    for (int k : candidates)
    {
        const Entry& e = ix.entries[size_t(k)];
        const double f = whole_title ? Fit(ix, words, e.all) : Score(ix, words, head, e);
        scored.emplace_back(k, f);
        if (f > best_fit) { second_fit = best_fit; best_fit = f; best = k; }
        else if (f > second_fit) second_fit = f;
        // what the whole query says, apart from the main-title tiers: a game that explains all of it comes first
        // ("Front Mission - Gun Hazard" is "Front Mission Series - Gun Hazard", not the "Front Mission" its head names)
        const double w = whole_title ? f : std::max(Fit(ix, words, e.all), Fit(ix, words, e.head));
        if (w > whole_fit) { second_whole = whole_fit; whole_fit = w; whole = k; }
        else if (w > second_whole) second_whole = w;
    }
    if (whole >= 0 && whole_fit >= 0.85 && whole_fit - second_whole >= 0.05) best = whole;
    else if (best < 0 || best_fit < 0.85) return "";
    else if (best_fit - second_fit < 0.05)
    {
        best = usa_on_tie ? UsaOfTwo(ix, scored, best_fit) : -1;
        if (best < 0) return "";
    }
    return Pick(ix, ix.by_joined.at(ix.entries[size_t(best)].joined), q.region);
}
// The art name of the game this file name means, or "" when it cannot be told from others.
// region_hint: the region to prefer when the name carries none.
// usa_on_tie: when games fit equally well, take the USA one of two games that share a main title (UsaOfTwo), instead of none.
// whole_title: the whole title must fit (not only its main part before " - "), for lists of variants and hacks.
inline std::string Resolve(const Index& ix, const std::string& requested, const std::string& region_hint = "", bool usa_on_tie = false,
                           bool whole_title = false)
{
    Parsed q = Parse(requested);
    if (q.region.empty()) q.region = region_hint;
    const std::vector<std::string> words = Tokens(q.title);
    if (words.empty()) return "";
    std::string joined;
    for (const std::string& w : words) joined += w;
    const auto same = ix.by_joined.find(joined);
    if (same != ix.by_joined.end()) return Pick(ix, same->second, q.region);
    const std::string found = ResolveTitle(ix, q, words, usa_on_tie, whole_title);
    if (!found.empty()) return found;
    // nothing fits: one of a game's other titles, exactly ("Art of Fighting" is "Art of Fighting _ Ryuuko no Ken");
    // or the query's main title, before " - ", exactly the first of them ("Samurai Shodown III - Blades of Blood" is
    // "Samurai Shodown III _ Samurai Spirits - Zankurou Musouken"); only when those entries are one game
    auto oneGame = [&ix](const std::vector<int>& group) {
        for (int k : group)
            if (ix.entries[size_t(k)].joined != ix.entries[size_t(group[0])].joined) return false;
        return !group.empty();
    };
    const auto alternate = ix.by_alternate.find(joined);
    if (alternate != ix.by_alternate.end() && oneGame(alternate->second))
        return Pick(ix, ix.by_joined.at(ix.entries[size_t(alternate->second[0])].joined), q.region);
    std::string main;
    for (const std::string& w : Tokens(MainOf(q.title))) main += w;
    const auto first = main != joined ? ix.by_first.find(main) : ix.by_first.end();
    if (first != ix.by_first.end() && oneGame(first->second))
        return Pick(ix, ix.by_joined.at(ix.entries[size_t(first->second[0])].joined), q.region);
    return "";
}
// The entries of a sorted key list that equal key, and those that only start with it.
inline void Find(const std::vector<std::pair<std::string, int>>& sorted, const std::string& key, std::vector<int>& exact,
                 std::vector<int>& prefix)
{
    for (auto it = std::lower_bound(sorted.begin(), sorted.end(), std::make_pair(key, -1));
         it != sorted.end() && it->first.compare(0, key.size(), key) == 0; ++it)
        (it->first.size() == key.size() ? exact : prefix).push_back(it->second);
}
// The art name for a ROM's internal header title ("ZELDANODENSETSU", "FINAL FANTASY 6"): its letters, or its words,
// equal an art name's; or, for a title cut short (headers hold 21 characters), start exactly one game's; or Resolve
// finds it by its words. Country byte 0 (Japan) prefers (Japan).
inline std::string ResolveHeader(const Index& ix, const std::string& header_title, bool japan, bool usa_on_tie = false)
{
    const std::string letters = RawKey(header_title);
    std::string words;
    for (const std::string& w : Tokens(header_title)) words += w;
    if (letters.size() < 8) return "";
    for (const char* generic : {"supernintendo", "superfamicom", "supernes", "nintendo", "untitled", "notitle", "unknown"})
        if (letters == generic) return "";
    std::vector<int> exact, prefix, word_exact, word_prefix;
    Find(ix.by_raw, letters, exact, prefix);
    Find(ix.by_key, words, word_exact, word_prefix);
    // the strongest evidence that finds anything decides; the same title read two ways is one game
    const std::vector<int>* order[] = {&exact, &word_exact, letters.size() >= 12 ? &prefix : nullptr,
                                       words.size() >= 12 ? &word_prefix : nullptr};
    for (const std::vector<int>* found : order)
    {
        if (!found || found->empty()) continue;
        const std::string& game = ix.entries[size_t(found->front())].joined;
        for (int k : *found)
            if (ix.entries[size_t(k)].joined != game) return ""; // two different games: not told apart
        return Pick(ix, ix.by_joined.at(game), japan ? "Japan" : "");
    }
    // a title that leaves words out ("SUPER ROBOT TAISEN4" for "Dai-4-ji Super Robot Taisen"): the words, in any order
    return Resolve(ix, header_title, japan ? "Japan" : "", usa_on_tie);
}
} // namespace LibretroArtMatch
