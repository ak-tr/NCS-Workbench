// Session dialogs: start (prepare WORK), unfinished session on startup,
// finish, and plain confirmations/messages. All modal, Mosaic-styled.

#pragma once

#include <windows.h>

#include <vector>

#include "session.h"
#include "theme.h"
#include "xpstd.h"

// Stage titles, 1-7: "Prepare WORK", "Read the ECU", ...
const wchar_t* StageTitle(int stage);

// Lists what happens to FSW_PSW.TRC and FSW_PSW.MAN before anything does.
// True to start; *reuseTrc: use the TRC in WORK as the first read. The
// backup folder can be changed here (saved to the settings).
bool RunPrepareDialog(HWND owner, const WString& workDir, WString& backupDir, bool* reuseTrc);

enum RecoverChoice { RECOVER_RESUME, RECOVER_CANCEL };
// An unfinished session found at startup. Changes nothing itself.
RecoverChoice RunRecoverDialog(HWND owner, const Session& s);

// Results, outcome and clean-up; calls FinishSession. True once finished.
bool RunFinishDialog(HWND owner, Session* s);

struct ConfirmRow {
    WString name, meta, action;  // FSW_PSW.TRC / 2,167 B / 11:00 / what happens to it
};

struct ConfirmSpec {
    WString caption, label, title, body;
    std::vector<ConfirmRow> rows;
    WString okText;
    ButtonKind okKind;
    WString cancelText;  // empty: OK only
    bool focusOk;        // false: Cancel has focus (destructive actions)
};

bool RunConfirmDialog(HWND owner, const ConfirmSpec& spec);
void RunMessageDialog(HWND owner, const WString& caption, const WString& label, const WString& title,
                      const WString& body);

// "3 H 12 MIN AGO" for a file time.
WString FormatAge(const FILETIME& ft);

// History labels: VERIFIED / DISCREPANCIES / ... or IN PROGRESS, and their colour.
WString SessionResultText(const Session& s);
COLORREF SessionResultColor(const Session& s);
// "E89 / MK60_87.C0A", or "ECU NOT CONFIRMED".
WString SessionEcuText(const Session& s);

// Opens a session file in Notepad (TRC and MAN files have no association).
void OpenInNotepad(const WString& path);
