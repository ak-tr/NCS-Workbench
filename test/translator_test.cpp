// Tests for src/translator.cpp: finding and loading Translations.csv on a
// mock NCS Dummy folder in %TEMP%. Uses LoadTranslationsWith, so the
// registry settings are never touched. Built and run by test.cmd.

#include <windows.h>

#include <cassert>
#include <cstdio>
#include <cstring>

#include "../src/translator.h"
#include "../src/util.h"

#include "testfiles.h"

static const char kCsv[] = "CONTRIBUTORS,\"revtor,someone\"\r\nLASTMODIFIED,20201210\r\n\r\n"
                           "aktiv,Enabled\r\nBC_DIGITAL_V,Digital speed display\r\n0_1sec,\r\n";

int main()
{
    wchar_t tmp[MAX_PATH], name[64];
    GetTempPathW(MAX_PATH, tmp);
    wsprintfW(name, L"ncswb-translator-test-%lu", GetCurrentProcessId());
    WString root = JoinPath(NormalizeDir(tmp), name);
    RemoveTree(root);
    assert(EnsureDir(root) == 0);
    WString dummyDir = JoinPath(root, L"NCS Dummy");
    WString otherDir = JoinPath(root, L"Other");
    assert(EnsureDir(dummyDir) == 0 && EnsureDir(otherDir) == 0);
    WString dummyExe = JoinPath(dummyDir, L"NcsDummy.exe");
    WriteText(dummyExe, "MZ");
    // Not the usual case: found anyway.
    WString csv = JoinPath(dummyDir, L"translations.CSV");
    WriteText(csv, kCsv);
    WriteText(JoinPath(dummyDir, L"other.csv"), "x,y\r\n");

    // Automatic discovery next to NcsDummy.exe.
    assert(lstrcmpiW(FindDummyTranslations(dummyExe).c_str(), csv.c_str()) == 0);
    assert(FileNameOf(FindDummyTranslations(dummyExe)) == L"translations.CSV");
    assert(FindDummyTranslations(JoinPath(otherDir, L"NcsDummy.exe")).empty());
    assert(FindDummyTranslations(WString()).empty());

    TranslationConfig c;
    c.dummyExe = dummyExe;
    TranslationState s;
    LoadTranslationsWith(c, &s);
    assert(s.loaded && s.source == TSOURCE_NCS_DUMMY && s.problem.empty());
    assert(s.table.keywords == 3 && s.table.entries.size() == 2 && s.table.untranslated == 1);
    assert(s.table.lastModified == L"20201210" && s.table.contributors == L"revtor,someone");
    assert(*LookupTranslation(s.table, L"BC_DIGITAL_V") == L"Digital speed display");

    // Reload picks up a changed file; the source file is never written.
    WriteText(csv, "BC_DIGITAL_V,Changed\r\n");
    LoadTranslationsWith(c, &s);
    assert(s.loaded && *LookupTranslation(s.table, L"BC_DIGITAL_V") == L"Changed");
    String after;
    assert(ReadFileBytes(csv, &after) && after == "BC_DIGITAL_V,Changed\r\n");

    // No Translations.csv next to NCS Dummy: a problem that names where it looked.
    c.dummyExe = JoinPath(otherDir, L"NcsDummy.exe");
    LoadTranslationsWith(c, &s);
    assert(!s.loaded && s.source == TSOURCE_NCS_DUMMY && !s.problem.empty());
    assert(s.path == JoinPath(otherDir, L"Translations.csv"));
    assert(s.table.entries.empty());

    // NCS Dummy not set, or looking for it turned off.
    c.dummyExe.clear();
    LoadTranslationsWith(c, &s);
    assert(!s.loaded && s.source == TSOURCE_NONE && !s.problem.empty());
    c.dummyExe = dummyExe;
    c.autoFind = false;
    LoadTranslationsWith(c, &s);
    assert(!s.loaded && s.source == TSOURCE_NONE);

    // A chosen file works without NCS Dummy, and wins over the one next to it.
    WString manual = JoinPath(otherDir, L"My translations.csv");
    WriteText(manual, "BC_DIGITAL_V,Mine\r\n");
    c.file = manual;
    c.autoFind = true;
    LoadTranslationsWith(c, &s);
    assert(s.loaded && s.source == TSOURCE_FILE && s.path == manual);
    assert(*LookupTranslation(s.table, L"BC_DIGITAL_V") == L"Mine");
    c.dummyExe.clear();
    LoadTranslationsWith(c, &s);
    assert(s.loaded);

    // A chosen file that's gone, or isn't a translation list: reported, no quiet fallback.
    c.dummyExe = dummyExe;
    c.file = JoinPath(otherDir, L"missing.csv");
    LoadTranslationsWith(c, &s);
    assert(!s.loaded && s.source == TSOURCE_FILE && !s.problem.empty() && s.table.entries.empty());
    WString notCsv = JoinPath(otherDir, L"notes.csv");
    WriteText(notCsv, "just some notes\r\nwithout commas\r\n");
    c.file = notCsv;
    LoadTranslationsWith(c, &s);
    assert(!s.loaded && !s.problem.empty() && s.table.entries.empty());
    c.file = otherDir;  // a folder, not a file
    LoadTranslationsWith(c, &s);
    assert(!s.loaded);

    RemoveTree(root);
    printf("translator_test: all passed\n");
    return 0;
}
