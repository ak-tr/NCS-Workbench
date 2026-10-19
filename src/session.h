// Guided coding session: state machine, snapshots and the WORK-folder changes
// around NCS Expert and NCS Dummy. No UI here (main.cpp and sessiondlg.cpp
// draw it), so test/session_test.cpp can drive it on a temp folder.
//
// A session lives in <backup folder>\Sessions\<id>\: session.ini plus a
// read-only snapshot of every WORK file it took. Rules:
// - Only FSW_PSW.TRC and FSW_PSW.MAN in WORK are ever changed.
// - A WORK file is deleted or emptied only once its current content is in
//   the session folder (same SHA-256), checked again right before the change.
// - Captures wait until a file has stopped changing and nobody has it open
//   for writing; the same content is never captured twice.
// - Nothing is inferred about the coding from file events: the user reports
//   that NCS Expert coded the ECU, and only a read-back verifies values.

#pragma once

#include <windows.h>

#include <vector>

#include "trace.h"
#include "xpstd.h"

enum SessionState {
    SESSION_NONE,
    SESSION_PREPARING,          // 01: saving and clearing WORK, or stopped there by an error
    SESSION_AWAITING_READ,      // 02: waiting for the FSW_PSW.TRC of CODIERDATEN_LESEN
    SESSION_AWAITING_ECU,       // 02: trace captured, the user confirms which ECU it came from
    SESSION_AWAITING_MAN,       // 03: waiting for an FSW_PSW.MAN exported from NCS Dummy
    SESSION_REVIEWING,          // 04: newest export captured and compared
    SESSION_CODING,             // 05: reviewed; the user codes in NCS Expert
    SESSION_CODED,              // 06: the user reported coding; read-back not prepared yet
    SESSION_AWAITING_READBACK,  // 06: WORK cleared, waiting for the read-back trace
    SESSION_VERIFYING,          // 06: read-back captured and compared
    SESSION_COMPLETED,
    SESSION_CANCELLED,
    SESSION_FAILED,
};

enum SnapKind {
    SNAP_BEFORE_TRC,  // WORK as it was when the session started
    SNAP_BEFORE_MAN,
    SNAP_READ,        // the ECU read; the newest one is the session's original trace
    SNAP_DUMMY,       // copy of the original named CHASSIS-MODULE-...-FSW_PSW.TRC for NCS Dummy
    SNAP_MAN,         // each MAN export
    SNAP_WORK_TRC,    // WORK files saved before a read-back or at the finish
    SNAP_WORK_MAN,
    SNAP_READBACK,    // traces read after coding
};

enum Outcome { OUTCOME_NONE, OUTCOME_VERIFIED, OUTCOME_DISCREPANCIES, OUTCOME_UNVERIFIED, OUTCOME_CANCELLED,
               OUTCOME_FAILED };

struct Snapshot {
    int id;  // stable, 1-based: [snap.<id>] in session.ini
    SnapKind kind;
    WString file;  // name in the session folder
    String sha256;
    ULONGLONG size;
    SYSTEMTIME taken;
    WString source;  // the path it was copied from
};

// Polling state for one WORK file (not saved).
struct FileWatch {
    bool present;
    ULONGLONG size;
    FILETIME modified;
    DWORD since;   // tick count when size/time last changed
    bool handled;  // this size/time was already looked at
};

struct Session {
    WString id, dir, workDir;
    SessionState state;
    SYSTEMTIME started, prepared, reviewedAt, codedAt, finished;
    bool reuseTrc;  // the trace already in WORK was used as the first read
    WString chassis, daten, module, vehicle;  // confirmed by the user (E91, E89, MK60_87.C0A, free text)
    std::vector<Snapshot> snaps;
    int reviewed, coded;  // snapshot ids of the MAN export reviewed / reported coded, 0 = none
    // Snapshots from this id on count as the current read: after WORK's TRC
    // was deleted, any trace there is new, even one identical to an older read.
    int waitFrom;
    Outcome outcome;
    WString error;  // last failed operation, cleared by the next one

    // Saved for the history list: intended changes and the newest read-back's result.
    int changes;
    bool hasVerify;
    VerifySummary verify;

    // Not saved.
    FileWatch trcWatch, manWatch;
    WString notice;  // why a WORK file wasn't taken (shown in the status line)
};

// Time a file must stay unchanged before it's captured.
const DWORD kSettleMs = 1000;

void InitSession(Session* s);
bool IsSessionOpen(SessionState state);  // started and not finished
int SessionStage(SessionState state);    // 1-7, 0 for none
const wchar_t* OutcomeText(Outcome outcome);  // "VERIFIED", ... for labels

WString SessionsRoot(const WString& backupDir);
WString SnapshotPath(const Session& s, const Snapshot& snap);
const Snapshot* FindSnapshot(const Session& s, int id);
const Snapshot* LastSnapshot(const Session& s, SnapKind kind);
int CountSnapshots(const Session& s, SnapKind kind);

bool SaveSession(Session* s);  // atomic: temp file, then replace; sets error on failure
bool LoadSession(const WString& dir, Session* s);
// Every session under root, newest first.
std::vector<Session> ListSessions(const WString& sessionsRoot);

// Each returns "" on success or an error to show; on error WORK is as it was,
// or every changed file is already saved in the session folder.

// New folder Sessions\YYYYMMDD-HHMMSS, saved in state PREPARING.
WString CreateSession(Session* s, const WString& sessionsRoot, const WString& workDir, bool reuseTrc);
// Saves FSW_PSW.TRC and FSW_PSW.MAN, deletes the TRC (kept when reuseTrc),
// empties or creates the MAN. Safe to call again after an error.
WString PrepareWorkspace(Session* s);
// Looks at the file the state waits for and captures it once settled.
// True if anything visible changed (state, snapshot or notice).
bool PollSession(Session* s, DWORD now);
// AWAITING_ECU: records the ECU and writes the NCS Dummy copy; -> AWAITING_MAN.
WString ConfirmEcu(Session* s, const WString& chassis, const WString& daten, const WString& module,
                   const WString& vehicle);
// AWAITING_ECU: wrong ECU; deletes the (saved) WORK trace; -> AWAITING_READ.
WString ReadAgain(Session* s);
// REVIEWING: WORK's MAN must still be the reviewed export; -> CODING.
WString ReadyToCode(Session* s);
WString BackToReview(Session* s);  // CODING -> REVIEWING
// CODING: the user says NCS Expert coded the ECU; -> CODED.
WString ConfirmCoded(Session* s);
// CODED or VERIFYING: saves and deletes the TRC, saves and empties the MAN; -> AWAITING_READBACK.
WString PrepareReadback(Session* s);
// After coding (CODED, AWAITING_READBACK, VERIFYING). The outcome must fit
// the newest read-back (OutcomeAllowed). emptyMan: save, then empty a non-empty WORK MAN.
WString FinishSession(Session* s, Outcome outcome, bool emptyMan);
WString CancelSession(Session* s);

// Evidence: what the user may report at the finish.
bool OutcomeAllowed(const Session& s, Outcome outcome);
Outcome DerivedOutcome(const Session& s);

// The original trace against a MAN export (0 = newest).
struct SessionReview {
    bool loaded;
    TraceCheck trc, man;
    std::vector<DiffRow> rows;
    DiffSummary summary;
    int changes;
};
bool LoadReview(const Session& s, int manSnapId, SessionReview* review);

// The coded export against the newest read-back.
struct SessionVerification {
    bool loaded;
    TraceCheck readback;
    std::vector<VerifyRow> rows;
    VerifySummary summary;
};
bool LoadVerification(const Session& s, SessionVerification* v);

// Copies the session folder to parent\<id> (or <id>-2, ...). Returns "" or an error.
WString ExportSession(const Session& s, const WString& parent, WString* created);

// One session at a time per WORK folder, across Workbench windows. NULL if
// another window holds it; CloseHandle to release.
HANDLE LockWorkDir(const WString& workDir);

// Helpers shared with the UI.
bool ProbeFile(const WString& path, ULONGLONG* size, FILETIME* modified);
WString FormatClock(const SYSTEMTIME& t);  // 14:12
WString FormatDay(const SYSTEMTIME& t);    // 19 OCT 14:12
bool HasTime(const SYSTEMTIME& t);
