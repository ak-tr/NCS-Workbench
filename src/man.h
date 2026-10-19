// Empty MAN: confirm, back up FSW_PSW.MAN to <backupDir>\YYYYMMDD-HHMM-FSW_PSW.MAN
// (never overwriting), then truncate it to 0 bytes for a fresh NCS Dummy edit.

#pragma once

#include <windows.h>

#include "xpstd.h"

// Modal. Returns true and the backup's path once the MAN was backed up and emptied.
// The user can change backupDir in the dialog; the change is saved to the settings.
bool RunEmptyManDialog(HWND owner, const WString& manPath, WString& backupDir, WString* backup);
