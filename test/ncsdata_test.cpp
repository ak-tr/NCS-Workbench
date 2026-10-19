// Tests for src/ncsdata.h. Built and run by test.cmd.
//
// SP-Daten is BMW's data and stays out of the repo, so the files here are
// built record by record in the same format.

#include <cassert>
#include <cstdio>
#include <cstring>

#include "../src/ncsdata.h"

typedef std::vector<unsigned char> Bytes;

static void Record(Bytes& out, unsigned type, const Bytes& payload)
{
    out.push_back((unsigned char)payload.size());
    out.push_back((unsigned char)(type & 0xFF));
    out.push_back((unsigned char)(type >> 8));
    out.insert(out.end(), payload.begin(), payload.end());
    out.push_back(0x5A);  // check byte, ignored
}

static void Put(Bytes& b, const char* s)
{
    b.insert(b.end(), s, s + strlen(s) + 1);
}

static void PutWord(Bytes& b, unsigned w)
{
    b.push_back((unsigned char)(w & 0xFF));
    b.push_back((unsigned char)(w >> 8));
}

// Header defining one table: id, name, format, field names.
static void Table(Bytes& out, unsigned id, const char* name, const char* format, const char* fields)
{
    Bytes p;
    PutWord(p, id);
    Put(p, name);
    Record(out, 0x0300, p);
    p.clear();
    Put(p, format);
    Record(out, 0x0400, p);
    p.clear();
    Put(p, fields);
    Record(out, 0x0500, p);
}

static void EndHeader(Bytes& out)
{
    Record(out, 0xFF00, Bytes());
}

static Bytes KeywordFile()
{
    Bytes f;
    Table(f, 0, "DATEINAME", "S", "NAME");
    Table(f, 1, "SWT_EINTRAG", "WS", "KEYID,KEYWORD");
    EndHeader(f);
    const char* names[] = { "SZL", "C0A_V_EINHEIT", "C0A_SETZ_V_MAX", "KOMBI_X" };
    unsigned ids[] = { 0x14D8, 0x144C, 0x1440, 0x0200 };
    for (int i = 0; i < 4; ++i) {
        Bytes p;
        PutWord(p, ids[i]);
        Put(p, names[i]);
        Record(f, 1, p);
    }
    f.push_back(0);  // trailing bytes are ignored
    return f;
}

// PARZUWEISUNG_FSW record: {L} count + longs, L, W, then the function id.
static Bytes FswRecord(unsigned id, int longs)
{
    Bytes p;
    p.push_back((unsigned char)longs);
    for (int i = 0; i < longs * 4 + 4 + 2; ++i)
        p.push_back(0);
    PutWord(p, id);
    p.push_back(0x01);  // INDEX etc.
    return p;
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
    std::vector<int> hits;
    hits.push_back(CountSharedFunctions(traceIds, ParseModuleFunctions(&kombiFile[0], kombiFile.size())));
    hits.push_back(CountSharedFunctions(traceIds, ParseModuleFunctions(&dscFile[0], dscFile.size())));
    assert(hits[0] == 1 && hits[1] == 3);
    // 3 of the 3 names the table knows is a clear match...
    assert(PickModule(hits, (int)traceIds.size()) == 1);
    // ...but the app counts all trace functions (4), and 3 of 4 is under 80%.
    assert(PickModule(hits, (int)trace.size()) == -1);

    std::vector<int> unclear;
    unclear.push_back(5);
    unclear.push_back(3);
    assert(PickModule(unclear, 5) == -1);  // runner-up too close
    std::vector<int> weak;
    weak.push_back(2);
    assert(PickModule(weak, 10) == -1);  // under 80%
    assert(PickModule(std::vector<int>(), 3) == -1);
}

int main()
{
    TestRecords();
    TestKeywords();
    TestModuleFunctions();
    TestChassisMap();
    TestDetection();
    printf("ncsdata_test: all passed\n");
    return 0;
}
