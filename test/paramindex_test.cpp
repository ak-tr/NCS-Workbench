// Tests for src/paramindex.h (Parameter Explorer's index and search) on
// synthetic module files. Built and run by test.cmd.

#include <cassert>
#include <cstdio>
#include <cstring>

#include "../src/paramindex.h"

#include "ncsfiles.h"

enum { BC_DIGITAL_V = 0x0100, BC_DIGITAL_V_KORREKTUR = 0x0101, FH_KALTUEBERWACHUNG = 0x0200, SPIEGEL = 0x0300,
       UNNAMED = 0x0999, ONLY_IN_TABLE = 0x0400 };
enum { AKTIV = 0x01, NICHT_AKTIV = 0x02, WERT_01 = 0x03, UNKNOWN_VALUE = 0x77 };

static std::map<String, unsigned> Fsw()
{
    std::map<String, unsigned> k;
    k["BC_DIGITAL_V"] = BC_DIGITAL_V;
    k["BC_DIGITAL_V_KORREKTUR"] = BC_DIGITAL_V_KORREKTUR;
    k["FH_KALTUEBERWACHUNG"] = FH_KALTUEBERWACHUNG;
    k["SPIEGEL_ANKLAPPEN"] = SPIEGEL;
    k["NOT_IN_ANY_MODULE"] = ONLY_IN_TABLE;
    return k;
}

static std::map<String, unsigned> Psw()
{
    std::map<String, unsigned> k;
    k["aktiv"] = AKTIV;
    k["nicht_aktiv"] = NICHT_AKTIV;
    k["wert_01"] = WERT_01;
    return k;
}

static void Add(ParamIndexBuilder* b, const char* file, const Bytes& data, const std::map<String, String>& families)
{
    AddModuleToIndex(b, file, data.empty() ? NULL : &data[0], data.size(), families);
}

// KOMBI in two coding indexes, FRM with one function, a damaged file, an unreadable one.
static ParamIndex BuildIndex(bool withFsw = true)
{
    std::map<String, String> families;
    families["KMBI_E89.C05"] = "KOMBI";
    families["KMBI_E89.C07"] = "KOMBI";
    families["FRM_87.C10"] = "FRM";

    unsigned onOff[] = { AKTIV, NICHT_AKTIV }, off[] = { NICHT_AKTIV }, odd[] = { WERT_01, UNKNOWN_VALUE };
    ParamIndexBuilder b;
    ModuleBuilder c07(0x07);
    c07.Function(BC_DIGITAL_V, onOff, 2).Function(BC_DIGITAL_V_KORREKTUR, onOff, 2).Function(UNNAMED, odd, 2);
    ModuleBuilder c05(0x05);
    c05.Function(BC_DIGITAL_V, off, 1).Direct(SPIEGEL);
    ModuleBuilder frm(-1);  // no SGID_CODIERINDEX: the index comes from the name
    frm.Function(FH_KALTUEBERWACHUNG, onOff, 2);
    Add(&b, "KMBI_E89.C07", c07.f, families);  // out of order on purpose
    Add(&b, "KMBI_E89.C05", c05.f, families);
    Add(&b, "frm_87.c10", frm.f, families);
    const char junk[] = "not an SP-Daten file";
    Add(&b, "BROKEN.C01", Bytes(junk, junk + sizeof(junk)), families);
    AddUnreadableModule(&b, "LOCKED.C02");
    b.index.moduleFiles = 5;
    return FinishIndex(&b, withFsw ? Fsw() : std::map<String, unsigned>(), Psw());
}

static const IndexedFunction* Find(const ParamIndex& x, const char* keyword)
{
    for (size_t i = 0; i < x.functions.size(); ++i) {
        if (x.functions[i].keyword == keyword)
            return &x.functions[i];
    }
    return NULL;
}

static void TestIndex()
{
    ParamIndex x = BuildIndex();
    // Modules sorted by name, families and coding indexes kept apart.
    assert(x.modules.size() == 3);
    assert(x.modules[0].file == "frm_87.c10" && x.modules[0].family == "FRM" && x.modules[0].codingIndex == 0x10);
    assert(x.modules[1].file == "KMBI_E89.C05" && x.modules[1].family == "KOMBI" && x.modules[1].codingIndex == 5);
    assert(x.modules[2].file == "KMBI_E89.C07" && x.modules[2].codingIndex == 7);

    // One entry per function, listing every module file that has it.
    const IndexedFunction* v = Find(x, "BC_DIGITAL_V");
    assert(v && v->occurrences.size() == 2);
    assert(v->occurrences[0].module == 1 && v->occurrences[1].module == 2);  // C05 then C07
    assert(v->occurrences[0].values.size() == 1 && v->occurrences[0].values[0] == NICHT_AKTIV);
    assert(v->occurrences[1].values.size() == 2);
    const IndexedFunction* m = Find(x, "SPIEGEL_ANKLAPPEN");
    assert(m && m->occurrences.size() == 1 && m->occurrences[0].direct && m->occurrences[0].values.empty());

    // Only functions some module codes: the keyword table alone isn't enough.
    assert(!Find(x, "NOT_IN_ANY_MODULE"));
    assert(x.otherKeywords.size() == 1 && x.otherKeywords[0] == "NOT_IN_ANY_MODULE");

    // An id SWTFSW doesn't name is kept apart, after the named ones, and not searchable.
    assert(x.named == 4 && x.functions.size() == 5);
    assert(x.functions[4].id == UNNAMED && x.functions[4].keyword.empty());
    for (size_t i = 1; i < x.named; ++i)
        assert(x.functions[i - 1].keyword < x.functions[i].keyword);

    // Values are named from SWTPSW; an unknown id stays unnamed.
    assert(ValueName(x, AKTIV) && *ValueName(x, AKTIV) == "aktiv");
    assert(!ValueName(x, UNKNOWN_VALUE));

    // The damaged and unreadable files are reported, the rest still indexed.
    assert(x.problems.size() == 2);
    assert(x.problems[0].file == "BROKEN.C01" && x.problems[0].status == MODULE_NOT_NCS);
    assert(x.problems[1].file == "LOCKED.C02" && x.problems[1].status == MODULE_OK);
    assert(x.moduleFiles == 5 && x.functionNames && x.valueNamesRead && !x.cancelled);
}

static void TestMissingKeywordTables()
{
    ParamIndex x = BuildIndex(false);
    assert(!x.functionNames && x.named == 0);
    assert(x.functions.size() == 5);  // the module data is there, just not nameable
    assert(SearchIndex(x, L"DIGITAL", std::vector<WString>()).empty());
    assert(SearchIndex(x, L"", std::vector<WString>()).empty());
}

static std::vector<String> Keywords(const ParamIndex& x, const std::vector<SearchHit>& hits)
{
    std::vector<String> out;
    for (size_t i = 0; i < hits.size(); ++i)
        out.push_back(x.functions[hits[i].function].keyword);
    return out;
}

static void TestSearch()
{
    ParamIndex x = BuildIndex();
    std::vector<WString> none;

    // Exact first, then prefix, then contains; case doesn't matter.
    std::vector<SearchHit> h = SearchIndex(x, L"bc_digital_v", none);
    std::vector<String> k = Keywords(x, h);
    assert(k.size() == 2 && k[0] == "BC_DIGITAL_V" && k[1] == "BC_DIGITAL_V_KORREKTUR");
    assert(h[0].rank == RANK_EXACT && h[1].rank == RANK_PREFIX);

    h = SearchIndex(x, L"DIGITAL_V", none);
    k = Keywords(x, h);
    assert(k.size() == 2 && h[0].rank == RANK_KEYWORD && k[0] == "BC_DIGITAL_V");

    h = SearchIndex(x, L"kaltueberwachung", none);
    assert(h.size() == 1 && Keywords(x, h)[0] == "FH_KALTUEBERWACHUNG");

    h = SearchIndex(x, L"  ", none);  // empty query: every named function, in order
    assert(h.size() == x.named && h[0].function == 0);

    assert(SearchIndex(x, L"window", none).empty());
    assert(SearchIndex(x, L"NOT_IN_ANY_MODULE", none).empty());  // in SWTFSW, but no module has it
    // Values aren't functions: "aktiv" finds nothing.
    assert(SearchIndex(x, L"aktiv", none).empty());
}

static void TestTranslationSearch()
{
    ParamIndex x = BuildIndex();
    // Translations as the UI passes them: aligned with functions, folded, "" where none.
    std::vector<WString> t(x.functions.size());
    for (size_t i = 0; i < x.named; ++i) {
        if (x.functions[i].keyword == "BC_DIGITAL_V")
            t[i] = FoldString(L"On-board computer (OBC) digital speed display");
        else if (x.functions[i].keyword == "BC_DIGITAL_V_KORREKTUR")
            t[i] = FoldString(L"OBC digital speed display correction [aktiv: corrected speed, nicht_aktiv: true speed]");
        else if (x.functions[i].keyword == "FH_KALTUEBERWACHUNG")
            t[i] = FoldString(L"Window cold monitoring");
    }
    std::vector<SearchHit> h = SearchIndex(x, L"window", t);
    assert(h.size() == 1 && Keywords(x, h)[0] == "FH_KALTUEBERWACHUNG" && h[0].rank == RANK_WORDS);

    h = SearchIndex(x, L"Corrected Speed", t);  // inside the bracketed note
    assert(h.size() == 1 && Keywords(x, h)[0] == "BC_DIGITAL_V_KORREKTUR");

    h = SearchIndex(x, L"digital speed", t);  // words, not a phrase in the keyword
    assert(h.size() == 2);

    // Keyword matches rank above translation matches.
    h = SearchIndex(x, L"speed", t);
    assert(h.size() == 2 && h[0].rank == RANK_WORDS);
    h = SearchIndex(x, L"digital", t);
    assert(h[0].rank == RANK_KEYWORD);

    // Words may mix keyword and translation.
    h = SearchIndex(x, L"bc correction", t);
    assert(h.size() == 1 && Keywords(x, h)[0] == "BC_DIGITAL_V_KORREKTUR");

    // An untranslated function is found by keyword only.
    h = SearchIndex(x, L"spiegel", t);
    assert(h.size() == 1);
    assert(SearchIndex(x, L"mirror", t).empty());
}

static void TestCodingIndexFromName()
{
    assert(CodingIndexFromName("MK60_87.C0A") == 0x0A);
    assert(CodingIndexFromName("x.c1f") == 0x1F);
    assert(CodingIndexFromName("BR_REF.DAT") == -1);
    assert(CodingIndexFromName("NODOT") == -1);
    assert(CodingIndexFromName("A.CXY") == -1);
}

int main()
{
    TestIndex();
    TestMissingKeywordTables();
    TestSearch();
    TestTranslationSearch();
    TestCodingIndexFromName();
    printf("paramindex_test: all passed\n");
    return 0;
}
