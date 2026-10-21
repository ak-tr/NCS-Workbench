// Tests for CopyToUniqueName in src/util.cpp: backups never overwrite each other.
// Built and run by test.cmd.

#include <windows.h>

#include <cassert>
#include <cstdio>
#include <cstring>

#include "../src/util.h"

#include "testfiles.h"

// Now, moved by a number of seconds (negative = in the past).
static FILETIME NowPlus(LONGLONG seconds)
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULONGLONG t = (((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime) + seconds * 10000000LL;
    ft.dwLowDateTime = (DWORD)t;
    ft.dwHighDateTime = (DWORD)(t >> 32);
    return ft;
}

static void TestAges()
{
    assert(FormatShortAge(NowPlus(-20)) == L"JUST NOW");
    assert(FormatShortAge(NowPlus(-3 * 60 - 5)) == L"3 MIN AGO");
    assert(FormatShortAge(NowPlus(-(95 * 60 + 5))) == L"1 H 35 MIN AGO");
    assert(FormatShortAge(NowPlus(-(95 * 60 + 5)), true) == L"1 H AGO");
    assert(FormatShortAge(NowPlus(-3 * 60 - 5), true) == L"3 MIN AGO");
    assert(FormatShortAge(NowPlus(-(6 * 3600 - 30))) == L"5 H 59 MIN AGO");
    // Over 6 hours (or clearly in the future) the clock time or date comes back.
    FILETIME old = NowPlus(-(6 * 3600 + 120));
    assert(!IsRecent(old) && FormatShortAge(old) == FormatShortStamp(old));
    assert(IsRecent(NowPlus(20)) && FormatShortAge(NowPlus(20)) == L"JUST NOW");
    assert(!IsRecent(NowPlus(3600)));

    // Session times are local SYSTEMTIMEs.
    SYSTEMTIME local;
    GetLocalTime(&local);
    assert(FormatAge(LocalToFileTime(local)) == L"JUST NOW");
    // A local time from another season converts with that season's offset (summer time vs winter time).
    for (int days = 30; days <= 360; days += 30) {
        FILETIME then = NowPlus(-(LONGLONG)days * 86400);
        SYSTEMTIME utc, at;
        FileTimeToSystemTime(&then, &utc);
        SystemTimeToFileTime(&utc, &then);  // whole milliseconds, as a SYSTEMTIME holds
        SystemTimeToTzSpecificLocalTime(NULL, &utc, &at);
        FILETIME back = LocalToFileTime(at);
        assert(CompareFileTime(&back, &then) == 0);
    }
}

int main()
{
    TestAges();

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
