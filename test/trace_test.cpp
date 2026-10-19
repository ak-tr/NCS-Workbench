// Tests for src/trace.h and src/sha256.h. Built and run by test.cmd.

#include <cassert>
#include <cstdio>
#include <cstring>

#include "../src/sha256.h"
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

static void TestCheck()
{
    // A stray value, a duplicate with the same values, one with other values,
    // a name with odd characters, a function without values.
    const char* text = "\twert_99\nA\n\tw1\nB\n\tw1\nA\n\tw1\nB\n\tw2\n; note\nC\n";
    TraceCheck c;
    std::vector<TraceEntry> all = ScanTrace(text, strlen(text), &c);
    assert(all.size() == 6);   // repeats kept
    assert(c.functions == 4);  // A, B, "; note", C
    assert(c.strayValues == 1 && c.firstIssueLine == 1);
    assert(c.duplicates.size() == 2 && c.duplicates[0] == "A" && c.duplicates[1] == "B");
    assert(c.conflicting.size() == 1 && c.conflicting[0] == "B");
    assert(c.oddNames == 1);
    assert(c.emptyFunctions == 2);  // "; note" and C
    assert(c.binaryLines == 0 && IsPlausibleTrace(c));
    assert(TraceIssueCount(c) == 1 + 2 + 1 + 2);

    const char binary[] = "A\n\tw1\n\x01\x02junk\n";
    ScanTrace(binary, sizeof(binary) - 1, &c);
    assert(c.binaryLines == 1 && !IsPlausibleTrace(c) && c.firstIssueLine == 3);
    ScanTrace("", 0, &c);
    assert(c.functions == 0 && !IsPlausibleTrace(c));
    ScanTrace("\twert_01\n", 9, &c);
    assert(!IsPlausibleTrace(c));  // values only

    String trc = ReadSample("samples/FSW_PSW.TRC");
    ScanTrace(trc.data(), trc.size(), &c);
    assert(c.functions == 85 && TraceIssueCount(c) == 0 && IsPlausibleTrace(c));
}

static void TestVerify()
{
    // Intended: B w1->w2, C (w1,w2)->(w2,w2), E new, F w1->w4; D unchanged; A missing from the MAN.
    std::vector<TraceEntry> trc = Parse("A\n\tw1\nB\n\tw1\nC\n\tw1\n\tw2\nD\n\tw1\nF\n\tw1\n");
    std::vector<TraceEntry> man = Parse("B\n\tw2\nC\n\tw2\n\tw2\nD\n\tw1\nE\n\tw3\nF\n\tw4\n");
    std::vector<DiffRow> diff = DiffTrace(trc, man, NULL);
    assert(CountIntendedChanges(diff) == 4);

    // Read-back: B took, C kept its old values, E missing, F listed twice with different values.
    const char* rb = "A\n\tw1\nB\n\tw2\nC\n\tw1\n\tw2\nD\n\tw1\nF\n\tw4\nF\n\tw1\n";
    TraceCheck check;
    ScanTrace(rb, strlen(rb), &check);
    VerifySummary s;
    std::vector<VerifyRow> rows = VerifyReadback(diff, Parse(rb), check.conflicting, &s);
    assert(rows.size() == 4);
    assert(rows[0].name == "B" && rows[0].status == VERIFY_MATCH);
    assert(rows[1].name == "C" && rows[1].status == VERIFY_MISMATCH && JoinValues(rows[1].readback) == "w1, w2");
    assert(rows[2].name == "F" && rows[2].status == VERIFY_UNVERIFIABLE);  // ambiguous in the read-back
    assert(rows[3].name == "E" && rows[3].status == VERIFY_UNVERIFIABLE && rows[3].readback.empty());
    assert(s.match == 1 && s.mismatch == 1 && s.unverifiable == 2);
}

static void TestSha256()
{
    assert(Sha256Hex("", 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    assert(Sha256Hex("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const char* two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";  // two blocks
    assert(Sha256Hex(two, strlen(two)) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    Sha256 h;  // fed in pieces
    for (int i = 0; i < 1000; ++i)
        h.Update("aaaaaaaaaa", 10);
    String a10k(10000, 'a');
    assert(h.HexDigest() == Sha256Hex(a10k.data(), a10k.size()));
}

int main()
{
    TestParseBasic();
    TestParseTolerant();
    TestDiff();
    TestPartialMan();
    TestSamples();
    TestCheck();
    TestVerify();
    TestSha256();
    printf("trace_test: all passed\n");
    return 0;
}
