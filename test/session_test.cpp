// Tests for src/session.cpp on a mock NCS Expert WORK folder in %TEMP%: no
// car, NCS Expert or NCS Dummy needed. Built and run by test.cmd.

#include <windows.h>

#include <cassert>
#include <cstdio>
#include <cstring>

#include "../src/session.h"
#include "../src/util.h"

#include "testfiles.h"

static const char kTrc[] = "SZL\r\n\twert_07\r\nC0A_V_EINHEIT\r\n\twert_01\r\nC0A_HSA\r\n\twert_01\r\n"
                           "C0A_DF_DIR\r\n\twert_00\r\n\twert_02\r\n";
// A partial MAN, as NCS Dummy may export it: only the changed functions.
static const char kMan1[] = "C0A_V_EINHEIT\r\n\twert_02\r\n";
static const char kMan2[] = "C0A_V_EINHEIT\r\n\twert_02\r\nC0A_HSA\r\n\twert_02\r\n";
static const char kMan3[] = "C0A_V_EINHEIT\r\n\twert_02\r\nC0A_HSA\r\n\twert_02\r\nC0A_DF_DIR\r\n\twert_01\r\n"
                            "\twert_02\r\n";
// After coding: V_EINHEIT and DF_DIR took, HSA didn't.
static const char kReadback[] = "SZL\r\n\twert_07\r\nC0A_V_EINHEIT\r\n\twert_02\r\nC0A_HSA\r\n\twert_01\r\n"
                                "C0A_DF_DIR\r\n\twert_01\r\n\twert_02\r\n";

static DWORD g_now = 1000;  // fake tick count for PollSession

struct Env {
    WString root, work, backup, sessions;
};

static Env MakeEnv(const wchar_t* tag)
{
    wchar_t tmp[MAX_PATH], name[64];
    GetTempPathW(MAX_PATH, tmp);
    wsprintfW(name, L"ncswb-session-test-%lu-%s", GetCurrentProcessId(), tag);
    Env e;
    e.root = JoinPath(NormalizeDir(tmp), name);
    RemoveTree(e.root);
    assert(EnsureDir(e.root) == 0);
    e.work = JoinPath(e.root, L"WORK");
    assert(EnsureDir(e.work) == 0);
    e.backup = JoinPath(e.root, L"Backups");  // created by CreateSession
    e.sessions = SessionsRoot(e.backup);
    return e;
}

static WString Trc(const Env& e)
{
    return JoinPath(e.work, L"FSW_PSW.TRC");
}

static WString Man(const Env& e)
{
    return JoinPath(e.work, L"FSW_PSW.MAN");
}

static ULONGLONG SizeOf(const WString& path)
{
    ULONGLONG size;
    FILETIME modified;
    assert(ProbeFile(path, &size, &modified));
    return size;
}

// One look, then another after the settle time: a file that stopped changing is taken.
static void Settle(Session* s)
{
    PollSession(s, g_now);
    g_now += kSettleMs;
    PollSession(s, g_now);
    g_now += 10;
}

static String SnapText(const Session& s, const Snapshot* snap)
{
    assert(snap);
    return ReadText(SnapshotPath(s, *snap));
}

// Started, prepared, trace captured and ECU confirmed.
static void StartToMan(const Env& e, Session* s)
{
    assert(CreateSession(s, e.sessions, e.work, false).empty());
    assert(PrepareWorkspace(s).empty());
    WriteText(Trc(e), kTrc);
    Settle(s);
    assert(s->state == SESSION_AWAITING_ECU);
    assert(ConfirmEcu(s, L"E91", L"E89", L"MK60_87.C0A", L"PZ12345").empty());
    assert(s->state == SESSION_AWAITING_MAN);
}

static void TestStartWithoutFiles()
{
    Env e = MakeEnv(L"empty");
    Session s;
    assert(CreateSession(&s, e.sessions, e.work, false).empty());
    assert(s.state == SESSION_PREPARING && FileExists(JoinPath(s.dir, L"session.ini")));
    assert(PrepareWorkspace(&s).empty());
    assert(s.state == SESSION_AWAITING_READ && s.error.empty());
    assert(!FileExists(Trc(e)));
    assert(SizeOf(Man(e)) == 0);  // created empty
    assert(s.snaps.empty());
    RemoveTree(e.root);
}

static void TestStartWithFiles()
{
    Env e = MakeEnv(L"files");
    WriteText(Trc(e), kTrc);
    WriteText(Man(e), kMan1);
    WriteText(JoinPath(e.work, L"ASW.TRC"), "unrelated");
    Session s;
    assert(CreateSession(&s, e.sessions, e.work, false).empty());
    assert(PrepareWorkspace(&s).empty());
    assert(s.state == SESSION_AWAITING_READ);
    const Snapshot* trc = LastSnapshot(s, SNAP_BEFORE_TRC);
    const Snapshot* man = LastSnapshot(s, SNAP_BEFORE_MAN);
    assert(SnapText(s, trc) == kTrc && SnapText(s, man) == kMan1);
    assert(GetFileAttributesW(SnapshotPath(s, *trc).c_str()) & FILE_ATTRIBUTE_READONLY);
    assert(!FileExists(Trc(e)));
    assert(SizeOf(Man(e)) == 0);
    assert(ReadText(JoinPath(e.work, L"ASW.TRC")) == "unrelated");
    RemoveTree(e.root);
}

static void TestReuseTrace()
{
    Env e = MakeEnv(L"reuse");
    WriteText(Trc(e), kTrc);
    Session s;
    assert(CreateSession(&s, e.sessions, e.work, true).empty());
    assert(PrepareWorkspace(&s).empty());
    assert(s.state == SESSION_AWAITING_ECU);
    assert(ReadText(Trc(e)) == kTrc);  // kept
    assert(SnapText(s, LastSnapshot(s, SNAP_READ)) == kTrc);
    Settle(&s);  // the same trace isn't taken twice
    assert(CountSnapshots(s, SNAP_READ) == 1);
    RemoveTree(e.root);
}

static void TestBackupFailureThenRetry()
{
    Env e = MakeEnv(L"locked");
    WriteText(Trc(e), kTrc);
    WriteText(Man(e), kMan1);
    // The MAN is open elsewhere with no sharing: it can't be saved, so nothing may change.
    HANDLE lock = CreateFileW(Man(e).c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    assert(lock != INVALID_HANDLE_VALUE);
    Session s;
    assert(CreateSession(&s, e.sessions, e.work, false).empty());
    WString err = PrepareWorkspace(&s);
    assert(!err.empty() && s.state == SESSION_PREPARING && s.error == err);
    assert(ReadText(Trc(e)) == kTrc);  // not deleted
    CloseHandle(lock);
    assert(ReadText(Man(e)) == kMan1);  // not emptied

    // Changed meanwhile: the retry saves the new version too, nothing is lost.
    WriteText(Trc(e), "SZL\r\n\twert_08\r\n");
    assert(PrepareWorkspace(&s).empty());
    assert(s.state == SESSION_AWAITING_READ);
    assert(CountSnapshots(s, SNAP_BEFORE_TRC) == 2 && CountSnapshots(s, SNAP_BEFORE_MAN) == 1);
    assert(SnapText(s, LastSnapshot(s, SNAP_BEFORE_TRC)) == "SZL\r\n\twert_08\r\n");
    assert(!FileExists(Trc(e)) && SizeOf(Man(e)) == 0);

    // A retry after success changes nothing.
    assert(!PrepareWorkspace(&s).empty());
    RemoveTree(e.root);
}

static void TestBackupFolderFailure()
{
    Env e = MakeEnv(L"nofolder");
    WriteText(Trc(e), kTrc);
    WriteText(e.backup, "a file where the backup folder should be");
    Session s;
    assert(!CreateSession(&s, e.sessions, e.work, false).empty());
    assert(ReadText(Trc(e)) == kTrc);
    RemoveTree(e.root);
}

static void TestCaptureTrace()
{
    Env e = MakeEnv(L"capture");
    Session s;
    assert(CreateSession(&s, e.sessions, e.work, false).empty());
    assert(PrepareWorkspace(&s).empty());

    // NCS Expert still writing: open for writing, half the file so far.
    HANDLE writer = CreateFileW(Trc(e).c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, 0, NULL);
    assert(writer != INVALID_HANDLE_VALUE);
    DWORD written;
    size_t half = sizeof(kTrc) / 2;
    WriteFile(writer, kTrc, (DWORD)half, &written, NULL);
    Settle(&s);
    Settle(&s);
    assert(s.state == SESSION_AWAITING_READ && CountSnapshots(s, SNAP_READ) == 0);
    WriteFile(writer, kTrc + half, (DWORD)(sizeof(kTrc) - 1 - half), &written, NULL);
    CloseHandle(writer);

    PollSession(&s, g_now);  // changed: not taken at first sight
    assert(s.state == SESSION_AWAITING_READ);
    g_now += kSettleMs;
    assert(PollSession(&s, g_now));
    assert(s.state == SESSION_AWAITING_ECU);
    assert(SnapText(s, LastSnapshot(s, SNAP_READ)) == kTrc);

    // Repeated change notifications for the same file: still one snapshot.
    for (int i = 0; i < 5; ++i)
        Settle(&s);
    assert(CountSnapshots(s, SNAP_READ) == 1);

    // Wrong ECU: read again, the same content counts as a new read.
    assert(ReadAgain(&s).empty());
    assert(s.state == SESSION_AWAITING_READ && !FileExists(Trc(e)));
    WriteText(Trc(e), kTrc);
    Settle(&s);
    assert(s.state == SESSION_AWAITING_ECU && CountSnapshots(s, SNAP_READ) == 2);
    RemoveTree(e.root);
}

static void TestMalformedTrace()
{
    Env e = MakeEnv(L"malformed");
    Session s;
    assert(CreateSession(&s, e.sessions, e.work, false).empty());
    assert(PrepareWorkspace(&s).empty());
    const char junk[] = "\x01\x02\x03\x00\xff garbage";
    WriteText(Trc(e), junk, sizeof(junk) - 1);
    Settle(&s);
    assert(s.state == SESSION_AWAITING_READ && !s.notice.empty() && CountSnapshots(s, SNAP_READ) == 0);
    WriteText(Trc(e), "", 0);  // empty
    Settle(&s);
    assert(s.state == SESSION_AWAITING_READ && CountSnapshots(s, SNAP_READ) == 0);
    WriteText(Trc(e), kTrc);
    Settle(&s);
    assert(s.state == SESSION_AWAITING_ECU && s.notice.empty());
    RemoveTree(e.root);
}

static void TestFullSession()
{
    Env e = MakeEnv(L"full");
    Session s;
    StartToMan(e, &s);
    const Snapshot* dummy = LastSnapshot(s, SNAP_DUMMY);
    assert(dummy && dummy->file.compare(0, 16, L"E89-MK60_87.C0A-") == 0);
    assert(dummy->file.size() > 12 && dummy->file.substr(dummy->file.size() - 12) == L"-FSW_PSW.TRC");
    assert(SnapText(s, dummy) == kTrc);

    // An empty MAN isn't an export.
    Settle(&s);
    assert(s.state == SESSION_AWAITING_MAN);

    // Two exports.
    WriteText(Man(e), kMan1);
    Settle(&s);
    assert(s.state == SESSION_REVIEWING && CountSnapshots(s, SNAP_MAN) == 1);
    WriteText(Man(e), kMan2);
    Settle(&s);
    assert(s.state == SESSION_REVIEWING && CountSnapshots(s, SNAP_MAN) == 2);
    SessionReview review;
    assert(LoadReview(s, 0, &review) && review.changes == 2 && review.summary.onlyTrc == 2);

    // Changed after the review, before the watcher noticed: needs another review.
    WriteText(Man(e), kMan3);
    assert(!ReadyToCode(&s).empty());
    assert(s.state == SESSION_REVIEWING && CountSnapshots(s, SNAP_MAN) == 3);
    assert(ReadyToCode(&s).empty());
    assert(s.state == SESSION_CODING && s.reviewed == LastSnapshot(s, SNAP_MAN)->id && s.changes == 3);

    // Changed while coding: "I have coded" sends it back to review.
    WriteText(Man(e), kMan2);
    assert(!ConfirmCoded(&s).empty());
    assert(s.state == SESSION_REVIEWING);
    WriteText(Man(e), kMan3);  // back to the reviewed content: a new export to review
    assert(!ReadyToCode(&s).empty());
    assert(ReadyToCode(&s).empty());
    assert(ConfirmCoded(&s).empty());
    assert(s.state == SESSION_CODED && s.coded == s.reviewed);
    assert(OutcomeAllowed(s, OUTCOME_UNVERIFIED) && !OutcomeAllowed(s, OUTCOME_VERIFIED));

    // Read-back: WORK cleared, both files saved first.
    size_t before = s.snaps.size();
    assert(PrepareReadback(&s).empty());
    assert(s.state == SESSION_AWAITING_READBACK);
    assert(!FileExists(Trc(e)) && SizeOf(Man(e)) == 0);
    assert(s.snaps.size() == before);  // TRC and MAN were already saved (same content)

    WriteText(Trc(e), kReadback);
    Settle(&s);
    assert(s.state == SESSION_VERIFYING && s.hasVerify);
    assert(s.verify.match == 2 && s.verify.mismatch == 1 && s.verify.unverifiable == 0);
    SessionVerification v;
    assert(LoadVerification(s, &v) && v.rows.size() == 3);
    assert(v.rows[1].name == "C0A_HSA" && v.rows[1].status == VERIFY_MISMATCH);
    assert(DerivedOutcome(s) == OUTCOME_DISCREPANCIES);
    assert(!OutcomeAllowed(s, OUTCOME_VERIFIED) && !OutcomeAllowed(s, OUTCOME_UNVERIFIED));

    // A user-picked status can't override the mismatch.
    assert(!FinishSession(&s, OUTCOME_VERIFIED, false).empty());
    assert(s.state == SESSION_VERIFYING);

    // Read back again: an identical trace is a new read-back.
    assert(PrepareReadback(&s).empty());
    WriteText(Trc(e), kReadback);
    Settle(&s);
    assert(s.state == SESSION_VERIFYING && CountSnapshots(s, SNAP_READBACK) == 2);

    // Restart: the record brings everything back.
    Session loaded;
    assert(LoadSession(s.dir, &loaded));
    assert(loaded.state == SESSION_VERIFYING && loaded.id == s.id && loaded.workDir == e.work);
    assert(loaded.chassis == L"E91" && loaded.daten == L"E89" && loaded.module == L"MK60_87.C0A");
    assert(loaded.vehicle == L"PZ12345" && loaded.reviewed == s.reviewed && loaded.coded == s.coded);
    assert(loaded.snaps.size() == s.snaps.size() && loaded.changes == 3 && loaded.hasVerify);
    assert(loaded.verify.mismatch == 1 && loaded.waitFrom == s.waitFrom);
    for (size_t i = 0; i < s.snaps.size(); ++i) {
        assert(loaded.snaps[i].id == s.snaps[i].id && loaded.snaps[i].kind == s.snaps[i].kind);
        assert(loaded.snaps[i].file == s.snaps[i].file && loaded.snaps[i].sha256 == s.snaps[i].sha256);
    }

    // Finish: a MAN left in WORK is saved, then emptied.
    WriteText(Man(e), "C0A_HSA\r\n\twert_02\r\n");
    assert(FinishSession(&loaded, OUTCOME_DISCREPANCIES, true).empty());
    assert(loaded.state == SESSION_COMPLETED && loaded.outcome == OUTCOME_DISCREPANCIES);
    assert(SizeOf(Man(e)) == 0);
    assert(SnapText(loaded, LastSnapshot(loaded, SNAP_WORK_MAN)) == "C0A_HSA\r\n\twert_02\r\n");

    std::vector<Session> list = ListSessions(e.sessions);
    assert(list.size() == 1 && list[0].state == SESSION_COMPLETED);

    WString exported;
    assert(EnsureDir(JoinPath(e.root, L"Export")) == 0);
    assert(ExportSession(loaded, JoinPath(e.root, L"Export"), &exported).empty());
    assert(ReadText(JoinPath(exported, L"session.ini")) == ReadText(JoinPath(loaded.dir, L"session.ini")));
    RemoveTree(e.root);
}

static void TestManVanishes()
{
    Env e = MakeEnv(L"vanish");
    Session s;
    StartToMan(e, &s);
    WriteText(Man(e), kMan1);
    Settle(&s);
    assert(s.state == SESSION_REVIEWING);
    WriteText(Man(e), "", 0);  // emptied before coding
    assert(!ReadyToCode(&s).empty());
    assert(s.state == SESSION_AWAITING_MAN);
    WriteText(Man(e), kMan1);  // the same export again
    Settle(&s);
    assert(s.state == SESSION_REVIEWING && CountSnapshots(s, SNAP_MAN) == 1);
    RemoveTree(e.root);
}

static void TestCancel()
{
    Env e = MakeEnv(L"cancel");
    Session s;
    StartToMan(e, &s);
    WriteText(Man(e), kMan1);
    assert(CancelSession(&s).empty());
    assert(s.state == SESSION_CANCELLED && s.outcome == OUTCOME_CANCELLED && HasTime(s.finished));
    assert(ReadText(Man(e)) == kMan1 && ReadText(Trc(e)) == kTrc);  // WORK untouched
    assert(!PollSession(&s, g_now));  // closed sessions don't watch
    assert(!CancelSession(&s).empty());
    Session loaded;
    assert(LoadSession(s.dir, &loaded) && loaded.state == SESSION_CANCELLED);
    RemoveTree(e.root);
}

static void TestLock()
{
    HANDLE a = LockWorkDir(L"C:\\NCSEXPER\\WORK");
    assert(a);
    assert(!LockWorkDir(L"c:\\ncsexper\\work"));  // same folder, any case
    HANDLE other = LockWorkDir(L"D:\\NCSEXPER\\WORK");
    assert(other);
    CloseHandle(a);
    CloseHandle(other);
    a = LockWorkDir(L"C:\\NCSEXPER\\WORK");
    assert(a);
    CloseHandle(a);
}

int main()
{
    TestStartWithoutFiles();
    TestStartWithFiles();
    TestReuseTrace();
    TestBackupFailureThenRetry();
    TestBackupFolderFailure();
    TestCaptureTrace();
    TestMalformedTrace();
    TestFullSession();
    TestManVanishes();
    TestCancel();
    TestLock();
    printf("session_test: all passed\n");
    return 0;
}
