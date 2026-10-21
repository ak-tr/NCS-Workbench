// Session dialogs: unfinished session on startup, finish, and plain
// confirmations/messages. All modal, Mosaic-styled. (Step 01 is drawn in the
// main window, main.cpp.)

#pragma once

#include <windows.h>

#include <vector>

#include "session.h"
#include "theme.h"
#include "xpstd.h"

// Stage titles, 1-7: "Prepare WORK", "Read the ECU", ...
const wchar_t* StageTitle(int stage);
// By type: MAN as above, VO 1-5 ("Prepare WORK", "Read the ECU", "Code from the VO", ...).
const wchar_t* StageTitle(SessionType type, int stage);

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

// History labels: VERIFIED / DISCREPANCIES / READ BACK / ... or IN PROGRESS, and their colour.
WString SessionResultText(const Session& s);
COLORREF SessionResultColor(const Session& s);
// "E89 / MK60_87.C0A", or "ECU NOT CONFIRMED".
WString SessionEcuText(const Session& s);
// The same, with " / VO" for VO sessions (where there's room: history, recovery).
WString SessionTitleText(const Session& s);

// Opens a session file in Notepad (TRC and MAN files have no association).
void OpenInNotepad(const WString& path);
