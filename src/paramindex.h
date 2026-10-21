// Parameter Explorer's index: which module files of one DATEN folder code
// which functions. Pure C++, no Windows headers; file formats in ncsdata.h.
//
// Only functions a module file actually lists are indexed. The keyword
// tables (SWTFSW) and NCS Dummy's translations name every keyword BMW
// uses, so neither says anything about which module has a function.

#pragma once

#include <algorithm>
#include <map>
#include <vector>

#include "ncsdata.h"
#include "translations.h"
#include "xpstd.h"

struct IndexedModule {
    String file;    // MK60_87.C0A
    String family;  // DSC, from SGET; "" if not listed there
    int codingIndex;  // from the file, else from the extension; -1 if neither
};

struct FunctionOccurrence {
    size_t module;  // index into ParamIndex::modules
    bool direct;
    std::vector<unsigned> values;  // value ids, as in the module file
};

struct IndexedFunction {
    unsigned id;
    String keyword;  // "" if SWTFSW doesn't name the id
    WString folded;  // keyword in lower case, for SearchIndex
    std::vector<FunctionOccurrence> occurrences;  // by module file name
};

struct ModuleProblem {
    String file;
    ModuleStatus status;  // MODULE_OK here means the file couldn't be read
};

struct ParamIndex {
    std::vector<IndexedModule> modules;     // indexed module files, sorted by name
    std::vector<IndexedFunction> functions;  // named ones by keyword, then unnamed by id
    size_t named = 0;                        // functions[0..named) have a keyword
    std::map<unsigned, String> valueNames;   // SWTPSW: value id -> keyword
    std::vector<String> otherKeywords;       // SWTFSW names no indexed module codes, sorted
    std::vector<ModuleProblem> problems;     // module files left out
    bool functionNames = false;              // SWTFSW was read
    bool valueNamesRead = false;             // SWTPSW was read
    int moduleFiles = 0;                     // module files found, read or not
    bool cancelled = false;                  // stopped before every file was read
};

// Built one module file at a time (the UI reads the files in slices).
struct ParamIndexBuilder {
    ParamIndex index;
    std::map<unsigned, std::vector<FunctionOccurrence> > byId;
};

// Coding index from a file name's extension (.C0A -> 0x0A), -1 if it has none.
inline int CodingIndexFromName(const String& file)
{
    size_t dot = file.rfind('.');
    if (dot == String::npos || file.size() != dot + 4 || (file[dot + 1] != 'C' && file[dot + 1] != 'c'))
        return -1;
    int v = 0;
    for (size_t i = dot + 2; i < file.size(); ++i) {
        char c = file[i];
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (d < 0)
            return -1;
        v = v * 16 + d;
    }
    return v;
}

inline String UpperAscii(String s)
{
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] >= 'a' && s[i] <= 'z')
            s[i] = char(s[i] - 'a' + 'A');
    }
    return s;
}

// Adds one module file. families: ParseModuleFamilies' map (may be empty).
inline void AddModuleToIndex(ParamIndexBuilder* b, const String& file, const unsigned char* data, size_t size,
                             const std::map<String, String>& families)
{
    ModuleCoding coding = ParseModuleCoding(data, size);
    if (coding.status != MODULE_OK) {
        ModuleProblem p = { file, coding.status };
        b->index.problems.push_back(p);
        return;
    }
    IndexedModule m;
    m.file = file;
    std::map<String, String>::const_iterator f = families.find(UpperAscii(file));
    m.family = f == families.end() ? String() : f->second;
    m.codingIndex = coding.codingIndex >= 0 ? coding.codingIndex : CodingIndexFromName(file);
    size_t moduleIndex = b->index.modules.size();
    b->index.modules.push_back(m);
    for (size_t i = 0; i < coding.functions.size(); ++i) {
        const ModuleFunction& fn = coding.functions[i];
        FunctionOccurrence o;
        o.module = moduleIndex;
        o.direct = fn.direct;
        o.values = fn.values;
        b->byId[fn.id].push_back(o);
    }
}

// A module file that couldn't be read at all.
inline void AddUnreadableModule(ParamIndexBuilder* b, const String& file)
{
    ModuleProblem p = { file, MODULE_OK };
    b->index.problems.push_back(p);
}

inline bool LessNoCaseAscii(const String& a, const String& b)
{
    return UpperAscii(a) < UpperAscii(b);
}

// Resolves function ids to keywords and sorts everything. fsw/psw are
// SWTFSW/SWTPSW name -> id tables; empty if the file is missing.
inline ParamIndex FinishIndex(ParamIndexBuilder* b, const std::map<String, unsigned>& fsw,
                              const std::map<String, unsigned>& psw)
{
    ParamIndex& x = b->index;
    x.functionNames = !fsw.empty();
    x.valueNamesRead = !psw.empty();
    x.valueNames = InvertKeywords(psw);
    std::map<unsigned, String> names = InvertKeywords(fsw);

    // Modules by file name; occurrences follow the new order.
    std::vector<size_t> order(x.modules.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::sort(order.begin(), order.end(),
              [&](size_t a, size_t c) { return LessNoCaseAscii(x.modules[a].file, x.modules[c].file); });
    std::vector<size_t> newIndex(order.size());
    std::vector<IndexedModule> sorted;
    for (size_t i = 0; i < order.size(); ++i) {
        newIndex[order[i]] = i;
        sorted.push_back(x.modules[order[i]]);
    }
    x.modules.swap(sorted);

    std::vector<IndexedFunction> named, unnamed;
    for (std::map<unsigned, std::vector<FunctionOccurrence> >::iterator i = b->byId.begin(); i != b->byId.end();
         ++i) {
        IndexedFunction f;
        f.id = i->first;
        f.occurrences.swap(i->second);
        for (size_t j = 0; j < f.occurrences.size(); ++j)
            f.occurrences[j].module = newIndex[f.occurrences[j].module];
        std::sort(f.occurrences.begin(), f.occurrences.end(),
                  [](const FunctionOccurrence& a, const FunctionOccurrence& c) { return a.module < c.module; });
        std::map<unsigned, String>::const_iterator n = names.find(f.id);
        if (n != names.end()) {
            f.keyword = n->second;
            for (size_t c = 0; c < f.keyword.size(); ++c)
                f.folded += FoldCase((wchar_t)(unsigned char)f.keyword[c]);
            named.push_back(f);
        } else {
            unnamed.push_back(f);
        }
    }
    std::sort(named.begin(), named.end(), [](const IndexedFunction& a, const IndexedFunction& c) {
        return a.keyword < c.keyword;
    });
    x.named = named.size();
    x.functions.swap(named);
    x.functions.insert(x.functions.end(), unnamed.begin(), unnamed.end());
    for (std::map<String, unsigned>::const_iterator i = fsw.begin(); i != fsw.end(); ++i) {
        if (b->byId.find(i->second) == b->byId.end())
            x.otherKeywords.push_back(i->first);  // map order: sorted
    }
    b->byId.clear();
    return x;
}

inline const String* ValueName(const ParamIndex& x, unsigned id)
{
    std::map<unsigned, String>::const_iterator i = x.valueNames.find(id);
    return i == x.valueNames.end() ? NULL : &i->second;
}

// ---------------------------------------------------------------------------
// Search

enum SearchRank { RANK_EXACT, RANK_PREFIX, RANK_KEYWORD, RANK_WORDS };

struct SearchHit {
    size_t function;  // index into ParamIndex::functions
    SearchRank rank;
};

// Searches the named functions. `translations` is aligned with
// ParamIndex::functions and already folded (FoldString); "" where there is
// none, or an empty vector when translations are off.
//
// Ranked: keyword equals the query, keyword starts with it, keyword contains
// it, then every word of the query appears in the keyword or the
// translation ("digital speed", "corrected speed"). Case is ignored; ties
// are in keyword order. An empty query lists every named function.
inline std::vector<SearchHit> SearchIndex(const ParamIndex& x, const WString& query,
                                          const std::vector<WString>& translations)
{
    std::vector<SearchHit> hits;
    WString q = FoldString(TrimSpaces(query));
    std::vector<WString> words;
    size_t start = 0;
    while (start < q.size()) {
        size_t end = q.find(L' ', start);
        if (end == WString::npos)
            end = q.size();
        if (end > start)
            words.push_back(q.substr(start, end - start));
        start = end + 1;
    }
    for (size_t i = 0; i < x.named; ++i) {
        if (q.empty()) {
            SearchHit h = { i, RANK_KEYWORD };
            hits.push_back(h);
            continue;
        }
        const WString& key = x.functions[i].folded;
        size_t at = key.find(q);
        SearchHit h = { i, RANK_WORDS };
        if (at != WString::npos) {
            h.rank = key.size() == q.size() ? RANK_EXACT : at == 0 ? RANK_PREFIX : RANK_KEYWORD;
            hits.push_back(h);
            continue;
        }
        const WString* t = i < translations.size() ? &translations[i] : NULL;
        bool all = !words.empty();
        for (size_t w = 0; w < words.size() && all; ++w)
            all = key.find(words[w]) != WString::npos || (t && t->find(words[w]) != WString::npos);
        if (all)
            hits.push_back(h);
    }
    // Functions are in keyword order, so their index breaks ties. (Not
    // stable_sort: its buffer pulls libc++abi in, see xpstd.h.)
    std::sort(hits.begin(), hits.end(), [](const SearchHit& a, const SearchHit& b) {
        return a.rank != b.rank ? a.rank < b.rank : a.function < b.function;
    });
    return hits;
}
