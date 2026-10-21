// Tests for src/daten.cpp on a mock SP-Daten install in %TEMP%: chassis
// discovery and Parameter Explorer's index job. Built and run by test.cmd.

#include <windows.h>

#include <cassert>
#include <cstdio>
#include <cstring>

#include "../src/daten.h"
#include "../src/util.h"

#include "ncsfiles.h"
#include "testfiles.h"

static void WriteBytes(const WString& path, const Bytes& data)
{
    WriteText(path, data.empty() ? "" : (const char*)&data[0], data.size());
}

static Bytes ChassisMap()
{
    Bytes f;
    Table(f, 1, "BR_ERSATZ", "SS", "BR1,BR2");
    EndHeader(f);
    const char* pairs[][2] = { { "E91", "E89" }, { "E89", "E89" }, { "E60", "E60" }, { "R56", "R56" } };
    for (int i = 0; i < 4; ++i) {
        Bytes p;
        Put(p, pairs[i][0]);
        Put(p, pairs[i][1]);
        Record(f, 1, p);
    }
    return f;
}

static Bytes Families()
{
    Bytes f;
    Table(f, 3, "SGAUSWAHL_SGBD", "SSSSSA{B}{", "SGNAME,CBD,CABD,SGBD,UMRSG,AUFTRAGSAUSDRUCK,INDEX");
    EndHeader(f);
    const char* cbd[] = { "C05", "C07" };
    for (int i = 0; i < 2; ++i) {
        Bytes p;
        Put(p, "KMBI_E89");
        Put(p, cbd[i]);
        Put(p, "A_E89KMB");
        Put(p, "KOMB87");
        Put(p, "KOMBI");
        Record(f, 3, p);
    }
    return f;
}

int main()
{
    wchar_t tmp[MAX_PATH], name[64];
    GetTempPathW(MAX_PATH, tmp);
    wsprintfW(name, L"ncswb-daten-test-%lu", GetCurrentProcessId());
    WString root = JoinPath(NormalizeDir(tmp), name);
    RemoveTree(root);
    assert(EnsureDir(root) == 0);
    WString work = JoinPath(root, L"WORK"), daten = JoinPath(root, L"DATEN");
    WString e89 = JoinPath(daten, L"E89"), e60 = JoinPath(daten, L"E60");
    assert(EnsureDir(work) == 0 && EnsureDir(daten) == 0 && EnsureDir(e89) == 0 && EnsureDir(e60) == 0);

    // Folders without module files offer nothing, so no chassis yet.
    WriteBytes(JoinPath(e89, L"BR_REF.DAT"), ChassisMap());
    assert(FindDatenDir(work) == daten);
    assert(LoadChassisList(daten).empty());

    // E89: keyword tables, families, two KOMBI coding indexes, a damaged module, other files.
    const char* fswNames[] = { "BC_DIGITAL_V", "BC_DIGITAL_V_KORREKTUR", "ONLY_IN_TABLE" };
    unsigned fswIds[] = { 0x100, 0x101, 0x400 };
    WriteBytes(JoinPath(e89, L"SWTFSW03.DAT"), KeywordFile(fswNames, fswIds, 3));
    const char* oldNames[] = { "OLD_NAME" };
    unsigned oldIds[] = { 0x100 };
    WriteBytes(JoinPath(e89, L"SWTFSW02.DAT"), KeywordFile(oldNames, oldIds, 1));  // the newest table is used
    const char* pswNames[] = { "aktiv", "nicht_aktiv" };
    unsigned pswIds[] = { 1, 2 };
    WriteBytes(JoinPath(e89, L"SWTPSW03.dat"), KeywordFile(pswNames, pswIds, 2));
    WriteBytes(JoinPath(e89, L"E89SGET.000"), Families());
    unsigned onOff[] = { 1, 2 };
    ModuleBuilder c05(0x05), c07(0x07);
    c05.Function(0x100, onOff, 2);
    c07.Function(0x100, onOff, 1).Function(0x101, onOff, 2);
    WriteBytes(JoinPath(e89, L"KMBI_E89.C05"), c05.f);
    WriteBytes(JoinPath(e89, L"KMBI_E89.C07"), c07.f);
    const char junk[] = "damaged";
    WriteBytes(JoinPath(e89, L"BROKEN.C01"), Bytes(junk, junk + sizeof(junk)));
    WriteBytes(JoinPath(e89, L"E89AT.M00"), Bytes(4, 0));  // not module files
    WriteBytes(JoinPath(e89, L"NOTES.CXX"), Bytes(4, 0));

    // Chassis discovery: BR_REF.DAT's chassis whose folder has module files (not the
    // empty E60, not the missing R56), plus a folder BR_REF.DAT doesn't name.
    WString extra = JoinPath(daten, L"F10X");
    assert(EnsureDir(extra) == 0);
    WriteBytes(JoinPath(extra, L"KMBI_F10.C01"), c05.f);
    std::vector<std::pair<WString, WString> > chassis = LoadChassisList(daten);
    assert(chassis.size() == 3);
    assert(chassis[0].first == L"E89" && chassis[1].first == L"E91" && chassis[2].first == L"F10X");
    assert(chassis[1].second == L"E89" && chassis[2].second == L"F10X");

    // Module detection still reads the newest SWTFSW (refactored onto LoadKeywords).
    std::vector<TraceEntry> trace(1);
    trace[0].name = "BC_DIGITAL_V";
    std::vector<unsigned> ids = TraceFunctionIds(e89, trace);
    assert(ids.size() == 1 && ids[0] == 0x100);
    assert(LoadKeywords(e89, L"SWTPSW*.DAT").size() == 2);
    assert(LoadKeywords(e60, L"SWTFSW*.DAT").empty());

    // The whole folder, one file per step.
    IndexJob job;
    assert(BeginIndexJob(e89, &job));
    assert(job.files.size() == 3);  // BROKEN.C01, KMBI_E89.C05, KMBI_E89.C07
    int steps = 1;
    while (StepIndexJob(&job))
        ++steps;
    assert(steps == 3);
    ParamIndex x = FinishIndexJob(&job, false);
    assert(!x.cancelled && x.moduleFiles == 3);
    assert(x.modules.size() == 2 && x.problems.size() == 1 && x.problems[0].file == "BROKEN.C01");
    assert(x.modules[0].file == "KMBI_E89.C05" && x.modules[0].family == "KOMBI" && x.modules[0].codingIndex == 5);
    assert(x.named == 2 && x.functions[0].keyword == "BC_DIGITAL_V");
    assert(x.functions[0].occurrences.size() == 2);  // in both coding indexes
    assert(x.functions[1].keyword == "BC_DIGITAL_V_KORREKTUR" && x.functions[1].occurrences.size() == 1);
    assert(x.otherKeywords.size() == 1 && x.otherKeywords[0] == "ONLY_IN_TABLE");
    assert(ValueName(x, 2) && *ValueName(x, 2) == "nicht_aktiv");

    // Stopped part way: what was read so far, flagged as incomplete.
    assert(BeginIndexJob(e89, &job));
    StepIndexJob(&job);
    StepIndexJob(&job);
    ParamIndex partial = FinishIndexJob(&job, true);
    assert(partial.cancelled && partial.modules.size() + partial.problems.size() == 2);

    // A folder without keyword tables: modules read, nothing nameable.
    WriteBytes(JoinPath(e60, L"KMBI_E60.C01"), c05.f);
    chassis = LoadChassisList(daten);  // with a module file, E60 is offered too
    assert(chassis.size() == 4 && chassis[0].first == L"E60" && chassis[0].second == L"E60");
    assert(BeginIndexJob(e60, &job));
    while (StepIndexJob(&job)) {
    }
    ParamIndex unnamed = FinishIndexJob(&job, false);
    assert(!unnamed.functionNames && unnamed.named == 0 && unnamed.modules.size() == 1);
    assert(unnamed.modules[0].family.empty());  // no SGET in E60

    // A folder that isn't there.
    assert(!BeginIndexJob(JoinPath(daten, L"E46"), &job));
    assert(FolderSignature(JoinPath(daten, L"E46")).empty());

    // The signature changes with the files, so a built index can tell it's stale.
    String before = FolderSignature(e89);
    assert(!before.empty() && FolderSignature(e89) == before);
    WriteBytes(JoinPath(e89, L"KMBI_E89.C09"), c05.f);
    String added = FolderSignature(e89);
    assert(added != before);
    WriteBytes(JoinPath(e89, L"KMBI_E89.C09"), c07.f);  // same name, new content
    assert(FolderSignature(e89) != added);

    RemoveTree(root);
    printf("daten_test: all passed\n");
    return 0;
}
