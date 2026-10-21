// Paths, settings (registry) and formatting helpers.

#pragma once

#include <windows.h>

#include "xpstd.h"

WString GetWindowTextString(HWND hwnd);
void Place(HWND hwnd, const RECT& r);  // MoveWindow to a rectangle
// Makes a list view's single column exactly as wide as the list: themed lists
// draw a divider at each column edge, and the scrollbar may come or go.
void FitListColumn(HWND list);
// NCS files are ASCII: byte -> character.
WString Widen(const String& s);

// Trims spaces/quotes and trailing slashes; keeps drive roots as "C:\".
WString NormalizeDir(WString p);
WString JoinPath(const WString& dir, const WString& name);
WString FileNameOf(const WString& path);
bool DirExists(const WString& dir);
bool FileExists(const WString& path);
// Creates the folder (one level) if it's missing. Returns 0 or a Windows error.
DWORD EnsureDir(const WString& dir);
// Copies to dir\<stem><tail>, never overwriting: if taken, tries <stem>-2<tail>,
// <stem>-3<tail>, ... Returns 0 or a Windows error (ERROR_FILE_EXISTS when
// every name up to -99 is taken).
DWORD CopyToUniqueName(const WString& source, const WString& dir, const WString& stem, const WString& tail,
                       WString* created);

// Settings live in HKEY_CURRENT_USER\Software\NCS Workbench (REG_SZ values).
// The key is only created on the first write.
void InitSettings();
void CloseSettings();
WString ReadSetting(const wchar_t* key, const wchar_t* def = L"");
void WriteSetting(const wchar_t* key, const WString& value);

// Whole file into memory (NCS files are a few KB; refuses anything over 8 MB).
bool ReadFileBytes(const WString& path, String* out);

// Folder picker; returns an empty string if cancelled.
WString PickFolder(HWND owner, const wchar_t* title, const WString& initial);
// Opens Explorer with the file selected (or the folder, if the file is gone).
void ShowInExplorer(const WString& path);
void CopyToClipboard(HWND owner, const WString& text);
WString SystemErrorText(DWORD code);

WString FormatThousands(ULONGLONG n);  // 12,345 (user's separator)
WString FormatShortSize(ULONGLONG n);  // 512 B, 18 KB, 1.2 MB
WString FormatStamp(const FILETIME& ft);       // 19 OCT 10:09, or 19 OCT 2025 if not this year
WString FormatShortStamp(const FILETIME& ft);  // 10:09 today, else 19 OCT
// JUST NOW, 3 MIN AGO, 1 H 35 MIN AGO (brief: 1 H AGO, for tight spots), 4 DAYS AGO
WString FormatAge(const FILETIME& ft, bool brief = false);
// Up to 6 hours ago, an age is quicker to read than a clock time.
bool IsRecent(const FILETIME& ft);
WString FormatShortAge(const FILETIME& ft, bool brief = false);  // FormatAge if recent, else FormatShortStamp
FILETIME LocalToFileTime(const SYSTEMTIME& t);  // a GetLocalTime value as a UTC file time
WString FormatFileStamp(const SYSTEMTIME& t);  // 20261019-1009, for backup names
