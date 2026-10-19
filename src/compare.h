// Compare: FSW_PSW.TRC against FSW_PSW.MAN, function by function (src/trace.h).

#pragma once

#include <windows.h>

#include "trace.h"
#include "xpstd.h"

// For step 03's status line. False if either file can't be read.
bool SummarizeDiff(const WString& trcPath, const WString& manPath, DiffSummary* summary);

// Modal, resizable window with the Function | TRC | MAN table.
void RunCompareDialog(HWND owner, const WString& trcPath, const WString& manPath);
