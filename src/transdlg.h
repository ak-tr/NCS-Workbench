// Translations in the UI: keyword cells with the English name next to the
// BMW keyword (translator.h), and the translations settings dialog.

#pragma once

#include <windows.h>

#include <vector>

#include "xpstd.h"

// Height of a list row that shows keywords: `single` (96-dpi px) on its own,
// taller while translations show (two lines: keyword and English name).
int KeywordRowHeight(int single);

// A keyword in mono `color`, with its translation as the settings say
// (below it, or above it as the main line). The keyword never goes away;
// without a translation it's drawn alone, centred.
void DrawKeywordText(HDC dc, const WString& keyword, const WString* translation, const RECT& cell, COLORREF color);
void DrawKeyword(HDC dc, const String& keyword, const RECT& cell, COLORREF color);
// Values joined with ", "; only a single value gets its translation.
void DrawValues(HDC dc, const std::vector<String>& values, const RECT& cell, COLORREF color);

// For screen readers: "C0A_V_EINHEIT (Speed unit)".
WString SpokenKeyword(const String& keyword);
WString SpokenValues(const std::vector<String>& values);

// Footer link text: "TRANSLATIONS / ON", "/ OFF" or "/ NONE".
WString TranslationsLinkText();

// Source file, load status, counts, metadata and display options.
// Returns true if anything changed, so the caller repaints. May change the
// NcsDummyPath setting.
bool RunTranslationsDialog(HWND owner);
