// Tests for CopyToUniqueName in src/util.cpp: backups never overwrite each other.
// Built and run by test.cmd.

#include <windows.h>

#include <cassert>
#include <cstdio>
#include <cstring>

#include "../src/util.h"

#include "testfiles.h"

int main()
{
    wchar_t tmp[MAX_PATH], name[40];
    GetTempPathW(MAX_PATH, tmp);
    wsprintfW(name, L"ncswb-util-test-%lu", GetCurrentProcessId());
    WString dir = JoinPath(NormalizeDir(tmp), name);
    DWORD err = EnsureDir(dir);
    assert(err == 0);
    WString source = JoinPath(dir, L"FSW_PSW.TRC");

    WString first, second;
    WriteText(source, "first");
    err = CopyToUniqueName(source, dir, L"E89-MK60_87.C0A-20261019-1012", L"-FSW_PSW.TRC", &first);
    assert(err == 0 && first == JoinPath(dir, L"E89-MK60_87.C0A-20261019-1012-FSW_PSW.TRC"));

    // Same minute again: a new "-2" file, and the first backup keeps its content.
    WriteText(source, "second");
    err = CopyToUniqueName(source, dir, L"E89-MK60_87.C0A-20261019-1012", L"-FSW_PSW.TRC", &second);
    assert(err == 0 && second == JoinPath(dir, L"E89-MK60_87.C0A-20261019-1012-2-FSW_PSW.TRC"));
    assert(ReadText(first) == "first");
    assert(ReadText(second) == "second");

    DeleteFileW(first.c_str());
    DeleteFileW(second.c_str());
    DeleteFileW(source.c_str());
    RemoveDirectoryW(dir.c_str());
    printf("util_test: all passed\n");
    return 0;
}
