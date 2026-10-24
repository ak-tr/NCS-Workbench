// Tests for src/ncsdata.h. Built and run by test.cmd.
//
// SP-Daten is BMW's data and stays out of the repo, so the files here are
// built record by record in the same format.

#include <cassert>
#include <cstdio>
#include <cstring>

#include "../src/ncsdata.h"

#include "ncsfiles.h"

static Bytes KeywordFile()
{
    const char* names[] = { "SZL", "C0A_V_EINHEIT", "C0A_SETZ_V_MAX", "KOMBI_X" };
    unsigned ids[] = { 0x14D8, 0x144C, 0x1440, 0x0200 };
    return KeywordFile(names, ids, 4);
}

static Bytes ModuleFile(const unsigned* ids, int count)
{
    Bytes f;
    Table(f, 0, "DATEINAME", "S", "NAME");
    Table(f, 16, "PARZUWEISUNG_PSW1", "W(B)", "PSW,DATUM");
    Table(f, 18, "PARZUWEISUNG_FSW", "{L}LWW{B}(B){B}{B}", "BLOCKNR,WORTADR,BYTEADR,FSW,INDEX,MASKE,EINHEIT,INDIVID");
    EndHeader(f);
    Bytes psw;
    PutWord(psw, 0x0BB8);  // a PSW record that must not be read as a function
    Record(f, 16, psw);
    for (int i = 0; i < count; ++i)
        Record(f, 18, FswRecord(ids[i], i % 2 ? 2 : 1));
    return f;
}

static void TestRecords()
{
    Bytes f = KeywordFile();
    std::vector<NcsRecord> recs = ParseNcsRecords(&f[0], f.size());
    assert(recs.size() == 6 + 1 + 4);
    NcsTable t = FindNcsTable(recs, "SWT_EINTRAG");
    assert(t.id == 1 && t.format == "WS");
    assert(FindNcsTable(recs, "MISSING").id == -1);
}

static void TestKeywords()
{
    Bytes f = KeywordFile();
    std::map<String, unsigned> k = ParseKeywordTable(&f[0], f.size());
    assert(k.size() == 4);
    assert(k["C0A_V_EINHEIT"] == 0x144C && k["SZL"] == 0x14D8);
}

static void TestModuleFunctions()
{
    unsigned ids[] = { 0x144C, 0x14D8, 0x144C };  // duplicate collapses
    Bytes f = ModuleFile(ids, 3);
    std::vector<unsigned> fn = ParseModuleFunctions(&f[0], f.size());
    assert(fn.size() == 2 && fn[0] == 0x144C && fn[1] == 0x14D8);

    Bytes keywords = KeywordFile();  // not a module file
    assert(ParseModuleFunctions(&keywords[0], keywords.size()).empty());
}

static void TestChassisMap()
{
    Bytes f;
    Table(f, 1, "BR_ERSATZ", "SS", "BR1,BR2");
    Table(f, 0, "BR_ZEILE", "BS", "GMZEICHEN,BAUREIHE");
    EndHeader(f);
    const char* pairs[][2] = { { "E91", "E89" }, { "E89", "E89" }, { "R56", "R56" } };
    for (int i = 0; i < 3; ++i) {
        Bytes p;
        Put(p, pairs[i][0]);
        Put(p, pairs[i][1]);
        Record(f, 1, p);
    }
    std::vector<std::pair<String, String> > m = ParseChassisMap(&f[0], f.size());
    assert(m.size() == 3);
    assert(m[0].first == "E91" && m[0].second == "E89");
    assert(m[2].first == "R56" && m[2].second == "R56");
}

static void TestDetection()
{
    Bytes kf = KeywordFile();
    std::map<String, unsigned> keywords = ParseKeywordTable(&kf[0], kf.size());
    std::vector<String> trace;
    trace.push_back("SZL");
    trace.push_back("C0A_V_EINHEIT");
    trace.push_back("C0A_SETZ_V_MAX");
    trace.push_back("NOT_IN_TABLE");
    std::vector<unsigned> traceIds = MapFunctionNames(trace, keywords);
    assert(traceIds.size() == 3);

    unsigned dsc[] = { 0x14D8, 0x144C, 0x1440, 0x0999 };
    unsigned kombi[] = { 0x0200, 0x14D8 };
    Bytes dscFile = ModuleFile(dsc, 4), kombiFile = ModuleFile(kombi, 2);
    std::vector<int> hits, sizes;
    hits.push_back(CountSharedFunctions(traceIds, ParseModuleFunctions(&kombiFile[0], kombiFile.size())));
    hits.push_back(CountSharedFunctions(traceIds, ParseModuleFunctions(&dscFile[0], dscFile.size())));
    sizes.push_back(2);
    sizes.push_back(4);
    assert(hits[0] == 1 && hits[1] == 3);
    // 3 of the 3 names the table knows is a clear match...
    assert(PickModule(hits, sizes, (int)traceIds.size()) == 1);
    // ...but the app counts all trace functions (4), and 3 of 4 is under 80%.
    assert(PickModule(hits, sizes, (int)trace.size()) == -1);

    // Coding indexes of one ECU, as a real KMBI_PL2 C08 trace (316 functions) matches them:
    // C08 and C09 both have every function, C09 codes far more the trace lacks.
    int kmbiHits[] = { 200, 236, 236, 316, 316 }, kmbiSizes[] = { 230, 261, 261, 329, 382 };
    std::vector<int> kh(kmbiHits, kmbiHits + 5), ks(kmbiSizes, kmbiSizes + 5);
    assert(PickModule(kh, ks, 316) == 3);
    ks[4] = 340;  // a C09 hardly bigger than C08 (24 differences vs 13): too close
    assert(PickModule(kh, ks, 316) == -1);
    ks[4] = 329;  // same function list: no way to tell them apart
    assert(PickModule(kh, ks, 316) == -1);

    std::vector<int> unclear(2, 5), unclearSizes(2, 6);
    assert(PickModule(unclear, unclearSizes, 5) == -1);  // tie
    std::vector<int> weak(1, 2), weakSizes(1, 2);
    assert(PickModule(weak, weakSizes, 10) == -1);  // under 80%
    assert(PickModule(std::vector<int>(), std::vector<int>(), 3) == -1);
}

static void TestModuleCoding()
{
    unsigned a[] = { 0x19, 0x66 }, b[] = { 0x07 };
    unsigned repeat[] = { 0x66, 0x08 };
    ModuleBuilder m(0x0A);
    m.Function(0x03C9, a, 2, true)  // a PSW2 record between values keeps the run going
        .Function(0x1416, b, 1)
        .Direct(0x2000)
        .Function(0x03C9, repeat, 2);  // listed again: merged, values unique
    ModuleCoding c = ParseModuleCoding(&m.f[0], m.f.size());
    assert(c.status == MODULE_OK);
    assert(c.codingIndex == 0x0A);
    assert(c.functions.size() == 3);
    assert(c.functions[0].id == 0x03C9 && !c.functions[0].direct);
    assert(c.functions[0].values.size() == 3 && c.functions[0].values[0] == 0x19 && c.functions[0].values[1] == 0x66 &&
           c.functions[0].values[2] == 0x08);
    assert(c.functions[1].id == 0x1416 && c.functions[1].values.size() == 1);
    assert(c.functions[2].id == 0x2000 && c.functions[2].direct && c.functions[2].values.empty());

    // Detection keeps using the value-list functions only.
    std::vector<unsigned> ids = ParseModuleFunctions(&m.f[0], m.f.size());
    assert(ids.size() == 2 && ids[0] == 0x03C9 && ids[1] == 0x1416);

    // Without SGID_CODIERINDEX the index is unknown here (the caller falls back to the name).
    ModuleBuilder noIndex(-1);
    noIndex.Function(0x0001, b, 1);
    assert(ParseModuleCoding(&noIndex.f[0], noIndex.f.size()).codingIndex == -1);

    // Not module files.
    Bytes keywords = KeywordFile();
    assert(ParseModuleCoding(&keywords[0], keywords.size()).status == MODULE_NO_FUNCTIONS);
    const unsigned char junk[] = { 'h', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd' };
    assert(ParseModuleCoding(junk, sizeof(junk)).status == MODULE_NOT_NCS);
    assert(ParseModuleCoding(junk, 0).status == MODULE_NOT_NCS);
    Bytes odd;
    Table(odd, 18, "PARZUWEISUNG_FSW", "WW", "A,B");  // a format this parser doesn't know
    EndHeader(odd);
    assert(ParseModuleCoding(&odd[0], odd.size()).status == MODULE_UNSUPPORTED);

    // A file cut short keeps the records before the cut.
    Bytes cut(m.f.begin(), m.f.begin() + (m.f.size() - 30));
    ModuleCoding partial = ParseModuleCoding(&cut[0], cut.size());
    assert(partial.status == MODULE_OK && !partial.functions.empty());
}

static void TestFamilies()
{
    Bytes f;
    Table(f, 2, "SGAUSWAHL_VM", "SSSSA{B}{", "SGNAME,CBD,UMRSG,VMG,AUFTRAGSAUSDRUCK,INDEX");
    Table(f, 3, "SGAUSWAHL_SGBD", "SSSSSA{B}{", "SGNAME,CBD,CABD,SGBD,UMRSG,AUFTRAGSAUSDRUCK,INDEX");
    EndHeader(f);
    Bytes p;
    Put(p, "MK60_87");
    Put(p, "C0A");
    Put(p, "A_E65");
    Put(p, "DSC_87");
    Put(p, "DSC");
    p.push_back(0x04);  // the AUFTRAGSAUSDRUCK expression, not read
    Record(f, 3, p);
    p.clear();
    Put(p, "kmbi_pl2");
    Put(p, "c05");
    Put(p, "KOMBI");
    Put(p, "VM");
    Record(f, 2, p);
    std::map<String, String> m = ParseModuleFamilies(&f[0], f.size());
    assert(m.size() == 2);
    assert(m["MK60_87.C0A"] == "DSC");
    assert(m["KMBI_PL2.C05"] == "KOMBI");  // keys are upper case
    Bytes none = KeywordFile();
    assert(ParseModuleFamilies(&none[0], none.size()).empty());
}

static void TestInvertKeywords()
{
    std::map<String, unsigned> k;
    k["AKTIV"] = 1;
    k["NICHT_AKTIV"] = 2;
    std::map<unsigned, String> inv = InvertKeywords(k);
    assert(inv.size() == 2 && inv[1] == "AKTIV" && inv[2] == "NICHT_AKTIV");
}

int main()
{
    TestRecords();
    TestKeywords();
    TestModuleFunctions();
    TestChassisMap();
    TestDetection();
    TestModuleCoding();
    TestFamilies();
    TestInvertKeywords();
    printf("ncsdata_test: all passed\n");
    return 0;
}
