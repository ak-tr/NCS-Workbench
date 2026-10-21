// Parameter Explorer: search every module file of an SP-Daten folder for a
// coding function and see which modules (and coding indexes) have it, with
// NCS Dummy's English names where loaded. Read-only, no session needed.

#pragma once

#include <windows.h>

#include "xpstd.h"

// Modal, resizable. SP-Daten is the DATEN folder next to workDir.
void RunExplorerDialog(HWND owner, const WString& workDir);
