// Backup TRC: chassis + module picker that copies FSW_PSW.TRC to
// <backupDir>\CHASSIS-MODULE-YYYYMMDD-HHMM-FSW_PSW.TRC, never overwriting.

#pragma once

#include <windows.h>

#include "xpstd.h"

// Modal. Returns true and the new file's path once a backup was written.
// The user can change backupDir in the dialog; the change is saved to the settings.
bool RunBackupDialog(HWND owner, const WString& source, WString& backupDir, WString* created);
