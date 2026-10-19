// Tests for src/trace.h. Built and run by test.cmd.

#include <cassert>
#include <cstdio>
#include <cstring>

#include "../src/trace.h"

static std::vector<TraceEntry> Parse(const char* text)
{
    return ParseTrace(text, strlen(text));
}

static String ReadSample(const char* path)
{
    FILE* f = fopen(path, "rb");
    assert(f && "run from the project root");
    String data;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        data.append(buf, n);
    fclose(f);
    return data;
}

static void TestParseBasic()
{
    std::vector<TraceEntry> e = Parse("SZL\r\n\twert_07\r\nC0A_DF_DIR\r\n\twert_00\r\n\twert_02\r\n");
    assert(e.size() == 2);
    assert(e[0].name == "SZL");
    assert(e[0].values.size() == 1 && e[0].values[0] == "wert_07");
    assert(e[1].name == "C0A_DF_DIR");
    assert(e[1].values.size() == 2 && e[1].values[0] == "wert_00" && e[1].values[1] == "wert_02");
}

static void TestParseTolerant()
{
    // LF only, space indent, trailing spaces, blank lines, no final newline,
    // a stray value before any name, a repeated function.
    std::vector<TraceEntry> e = Parse("\twert_99\n\nA\n    wert_01  \n\nB\n  wert_02\nA\n\twert_05");
    assert(e.size() == 2);
    assert(e[0].name == "A" && e[0].values.size() == 1 && e[0].values[0] == "wert_01");
    assert(e[1].name == "B" && e[1].values[0] == "wert_02");
    assert(Parse("").empty());
}

static void TestDiff()
{
    std::vector<TraceEntry> trc = Parse("A\n\tw1\nB\n\tw1\nC\n\tw1\n\tw2\nD\n\tw1\n");
    std::vector<TraceEntry> man = Parse("A\n\tw1\nB\n\tw2\nC\n\tw1\nE\n\tw3\n");
    DiffSummary s;
    std::vector<DiffRow> rows = DiffTrace(trc, man, &s);
    assert(rows.size() == 5);
    assert(rows[0].name == "A" && rows[0].status == DIFF_SAME);
    assert(rows[1].name == "B" && rows[1].status == DIFF_CHANGED);
    assert(rows[2].name == "C" && rows[2].status == DIFF_CHANGED);  // a value dropped
    assert(rows[3].name == "D" && rows[3].status == DIFF_ONLY_TRC && rows[3].man.empty());
    assert(rows[4].name == "E" && rows[4].status == DIFF_ONLY_MAN && rows[4].trc.empty());
    assert(s.same == 1 && s.changed == 2 && s.onlyTrc == 1 && s.onlyMan == 1);
    assert(!s.partialMan);
    assert(IsNotable(rows[3], s) && !IsNotable(rows[0], s));
    assert(JoinValues(rows[2].trc) == "w1, w2");
}

static void TestPartialMan()
{
    std::vector<TraceEntry> trc = Parse("A\n\tw1\nB\n\tw1\nC\n\tw1\nD\n\tw1\nE\n\tw1\n");
    std::vector<TraceEntry> man = Parse("B\n\tw2\n");
    DiffSummary s;
    std::vector<DiffRow> rows = DiffTrace(trc, man, &s);
    assert(s.partialMan && s.changed == 1 && s.onlyTrc == 4);
    assert(!IsNotable(rows[0], s));  // only in TRC is expected with a partial MAN
    assert(IsNotable(rows[1], s));
}

static void TestSamples()
{
    String trcData = ReadSample("samples/FSW_PSW.TRC");
    String manData = ReadSample("samples/FSW_PSW.MAN");
    std::vector<TraceEntry> trc = ParseTrace(trcData.data(), trcData.size());
    std::vector<TraceEntry> man = ParseTrace(manData.data(), manData.size());
    assert(trc.size() == 85 && man.size() == 85);
    assert(trc[0].name == "SZL" && trc[84].name == "C0B_TB_BDD");
    DiffSummary s;
    std::vector<DiffRow> rows = DiffTrace(trc, man, &s);
    assert(s.changed == 1 && s.onlyTrc == 0 && s.onlyMan == 0 && s.same == 84);
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].status == DIFF_CHANGED) {
            assert(rows[i].name == "C0A_V_EINHEIT");
            assert(JoinValues(rows[i].trc) == "wert_01" && JoinValues(rows[i].man) == "wert_02");
        }
    }
}

int main()
{
    TestParseBasic();
    TestParseTolerant();
    TestDiff();
    TestPartialMan();
    TestSamples();
    printf("trace_test: all passed\n");
    return 0;
}
