// Session history: past sessions with their files, changes and read-back results.

#pragma once

#include <windows.h>

#include "xpstd.h"

// Modal, resizable. Lists the sessions under sessionsRoot (newest first).
void RunHistoryDialog(HWND owner, const WString& sessionsRoot);
