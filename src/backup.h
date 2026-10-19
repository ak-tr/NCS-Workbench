// Backup TRC: chassis + module picker that copies FSW_PSW.TRC to
// <backupDir>\CHASSIS-MODULE-YYYYMMDD-HHMM-FSW_PSW.TRC, never overwriting.
// The same picker confirms a session's ECU (RunEcuDialog).

#pragma once

#include <windows.h>

#include "xpstd.h"

// Modal. Returns true and the new file's path once a backup was written.
// The user can change backupDir in the dialog; the change is saved to the settings.
bool RunBackupDialog(HWND owner, const WString& source, WString& backupDir, WString* created);

struct EcuChoice {
    WString chassis;  // as picked (E91)
    WString daten;    // its SP-Daten folder (E89), or the chassis without SP-Daten
    WString module;   // MK60_87.C0A
    WString vehicle;  // free text, may be empty; pre-fills the field
};

enum EcuResult { ECU_CLOSED, ECU_CONFIRMED, ECU_READ_AGAIN };

// Modal: which ECU a captured trace came from. The module detected from the
// trace is shown as a suggestion only. `stamp` (YYYYMMDD-HHMM) goes into the
// NCS Dummy file name shown.
EcuResult RunEcuDialog(HWND owner, const WString& trace, const WString& workDir, const WString& label,
                       const WString& info, const WString& stamp, EcuChoice* choice);
