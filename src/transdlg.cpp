// Translations in the UI. See transdlg.h.

#include <windows.h>
#include <commdlg.h>

#include "modal.h"
#include "theme.h"
#include "trace.h"
#include "transdlg.h"
#include "translator.h"
#include "util.h"

// ---------------------------------------------------------------------------
// Keyword cells

int KeywordRowHeight(int single)
{
    return S(TranslationsShown() ? single + 14 : single);
}

void DrawKeywordText(HDC dc, const WString& keyword, const WString* translation, const RECT& cell, COLORREF color)
{
    if (!translation) {
        DrawLabel(dc, g_fonts.mono, color, keyword.c_str(), cell, DT_LEFT | DT_VCENTER);
        return;
    }
    int cy = (cell.top + cell.bottom) / 2;
    RECT top = { cell.left, cy - S(18), cell.right, cy };
    RECT bottom = { cell.left, cy, cell.right, cy + S(18) };
    if (TranslationFirst()) {
        DrawLabel(dc, g_fonts.body, color, translation->c_str(), top, DT_LEFT | DT_BOTTOM);
        DrawLabel(dc, g_fonts.mono, kMuted, keyword.c_str(), bottom, DT_LEFT | DT_TOP);
    } else {
        DrawLabel(dc, g_fonts.mono, color, keyword.c_str(), top, DT_LEFT | DT_BOTTOM);
        DrawLabel(dc, g_fonts.body, kMuted, translation->c_str(), bottom, DT_LEFT | DT_TOP);
    }
}

void DrawKeyword(HDC dc, const String& keyword, const RECT& cell, COLORREF color)
{
    DrawKeywordText(dc, Widen(keyword), Translate(keyword), cell, color);
}

void DrawValues(HDC dc, const std::vector<String>& values, const RECT& cell, COLORREF color)
{
    DrawKeywordText(dc, Widen(JoinValues(values)), TranslateValues(values), cell, color);
}

WString SpokenKeyword(const String& keyword)
{
    const WString* t = Translate(keyword);
    return t ? Widen(keyword) + L" (" + *t + L")" : Widen(keyword);
}

WString SpokenValues(const std::vector<String>& values)
{
    const WString* t = TranslateValues(values);
    WString joined = Widen(JoinValues(values));
    return t ? joined + L" (" + *t + L")" : joined;
}

WString TranslationsLinkText()
{
    if (!Translations().loaded)
        return L"TRANSLATIONS / NONE";
    return TranslationsShown() ? L"TRANSLATIONS / ON" : L"TRANSLATIONS / OFF";
}

// ---------------------------------------------------------------------------
// Settings dialog

static const wchar_t kDialogClass[] = L"NcsWorkbenchTranslations";

enum {
    IDC_CHOOSE_FILE = 100,
    IDC_USE_DUMMY,
    IDC_CHOOSE_DUMMY,
    IDC_SHOW,
    IDC_AUTO,
    IDC_KEYWORD_FIRST,
    IDC_TRANSLATION_FIRST,
    IDC_RELOAD,
};

struct TranslationsDialog {
    HWND hwnd;
    HWND chooseFile, useDummy, chooseDummy, show, autoFind, keywordFirst, translationFirst, reload, close;
    bool done;
};

static TranslationsDialog* g_painting;  // PaintBuffered takes a plain function

static const int kWidth = 600, kHeight = 692;

static void UpdateControls(TranslationsDialog* d)
{
    int pad = S(24), right = S(kWidth) - pad;
    bool picked = Translations().source == TSOURCE_FILE;
    ShowWindow(d->useDummy, picked ? SW_SHOW : SW_HIDE);
    SetWindowTextW(d->chooseFile, picked ? L"CHANGE FILE" : L"CHOOSE FILE");
    Place(d->chooseFile, RightLink(d->chooseFile, right, S(132)));
    RECT f;
    GetWindowRect(d->chooseFile, &f);
    Place(d->useDummy, RightLink(d->useDummy, right - (f.right - f.left) - S(20), S(132)));
    SetWindowTextW(d->chooseDummy, ReadSetting(L"NcsDummyPath").empty() ? L"CHOOSE" : L"CHANGE");
    Place(d->chooseDummy, RightLink(d->chooseDummy, right, S(392)));
    SetThemedCheck(d->show, ReadSetting(L"TranslationsShow", L"1") != L"0");
    SetThemedCheck(d->autoFind, ReadSetting(L"TranslationsAuto", L"1") != L"0");
    SetThemedCheck(d->keywordFirst, !TranslationFirst());
    SetThemedCheck(d->translationFirst, TranslationFirst());
    InvalidateRect(d->hwnd, NULL, FALSE);
}

static WString FormatDateText(const WString& yyyymmdd)
{
    static const wchar_t* const kMonths[] = { L"JAN", L"FEB", L"MAR", L"APR", L"MAY", L"JUN",
                                              L"JUL", L"AUG", L"SEP", L"OCT", L"NOV", L"DEC" };
    if (yyyymmdd.size() != 8 || yyyymmdd.find_first_not_of(L"0123456789") != WString::npos)
        return yyyymmdd;  // not a date: as written
    int month = (yyyymmdd[4] - L'0') * 10 + (yyyymmdd[5] - L'0');
    if (month < 1 || month > 12)
        return yyyymmdd;
    WString day = yyyymmdd.substr(6, 2);
    if (day[0] == L'0')
        day.erase(0, 1);
    return day + L" " + kMonths[month - 1] + L" " + yyyymmdd.substr(0, 4);
}

static WString StatusLine(COLORREF* color)
{
    const TranslationState& s = Translations();
    if (!s.loaded) {
        *color = kWarn;
        return s.source == TSOURCE_NONE ? L"NOT LOADED / KEYWORDS SHOW UNTRANSLATED" : L"COULDN'T LOAD / KEYWORDS "
                                                                                       L"SHOW UNTRANSLATED";
    }
    const TranslationTable& t = s.table;
    *color = TranslationsShown() ? kGood : kMuted;
    return WString(TranslationsShown() ? L"LOADED / " : L"LOADED, HIDDEN / ") + FormatThousands(t.keywords) +
           L" KEYWORDS / " + FormatThousands(t.entries.size()) + L" TRANSLATED / " +
           FormatThousands(t.untranslated) + L" WITHOUT";
}

static WString DetailsLine()
{
    const TranslationState& s = Translations();
    const TranslationTable& t = s.table;
    WString line;
    if (!t.lastModified.empty())
        line = L"LAST MODIFIED " + FormatDateText(t.lastModified) + L" / ";
    line += t.encoding == TEXT_UTF8 ? L"UTF-8" : t.encoding == TEXT_UTF16LE ? L"UTF-16" : L"WINDOWS-1252";
    line += s.source == TSOURCE_FILE ? L" / FILE YOU CHOSE" : L" / NEXT TO NCS DUMMY";
    return line;
}

// What the parser skipped or couldn't be sure of; "" if nothing.
static WString DiagnosticsText()
{
    const TranslationTable& t = Translations().table;
    WString text;
    if (t.malformed)
        text += FormatThousands(t.malformed) + (t.malformed == 1 ? L" row" : L" rows") +
                L" couldn't be read and were skipped, the first at line " + FormatThousands(t.firstMalformedLine) +
                L". ";
    if (!t.conflicts.empty())
        text += FormatThousands(t.conflicts.size()) +
                (t.conflicts.size() == 1 ? L" keyword is" : L" keywords are") +
                L" listed more than once with different text (first: " + t.conflicts[0] +
                L"); the first text is used. ";
    else if (t.duplicates)
        text += FormatThousands(t.duplicates) + (t.duplicates == 1 ? L" keyword is" : L" keywords are") +
                L" listed more than once. ";
    if (t.undecodable)
        text += FormatThousands(t.undecodable) + L" characters couldn't be decoded and show as \xFFFD.";
    return text;
}

static void PaintDialog(HDC dc, const RECT& client)
{
    TranslationsDialog* d = g_painting;
    int pad = S(24), w = client.right;
    Fill(dc, client, kPage);
    const TranslationState& s = Translations();

    RECT r = { pad, S(24), w - pad, S(40) };
    DrawLabel(dc, g_fonts.label, kFaint, L"SETTINGS / TRANSLATIONS", r, DT_LEFT | DT_VCENTER, Tracking());
    r = { pad, S(44), w - pad, S(74) };
    DrawLabel(dc, g_fonts.heading, kWhite, L"English names from NCS Dummy", r, DT_LEFT | DT_VCENTER);
    r = { pad, S(78), w - pad, S(96) };
    DrawLabel(dc, g_fonts.label, kMuted, L"READ FROM YOUR NCS DUMMY'S TRANSLATIONS.CSV / NEVER CHANGED OR COPIED", r,
              DT_LEFT | DT_VCENTER, Tracking());
    RECT rule = { pad, S(110), w - pad, S(111) };
    Fill(dc, rule, kRule);

    RECT link;
    GetWindowRect(IsWindowVisible(d->useDummy) ? d->useDummy : d->chooseFile, &link);
    MapWindowPoints(NULL, d->hwnd, (POINT*)&link, 2);
    r = { pad, S(124), link.left - S(16), S(140) };
    DrawLabel(dc, g_fonts.label, kFaint, L"TRANSLATION FILE", r, DT_LEFT | DT_VCENTER, Tracking());
    r = { pad, S(146), w - pad, S(168) };
    if (s.path.empty())
        DrawLabel(dc, g_fonts.body, kSecondary, L"None", r, DT_LEFT | DT_VCENTER);
    else
        DrawLabel(dc, g_fonts.body, kWhite, s.path.c_str(), r, DT_LEFT | DT_VCENTER | DT_PATH_ELLIPSIS);
    COLORREF color;
    WString status = StatusLine(&color);
    r = { pad, S(174), w - pad, S(190) };
    DrawLabel(dc, g_fonts.label, color, status.c_str(), r, DT_LEFT | DT_VCENTER, Tracking());
    r = { pad, S(198), w - pad, S(250) };
    if (!s.loaded) {
        DrawLabel(dc, g_fonts.body, kText, s.problem.c_str(), r, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_EDITCONTROL);
    } else {
        RECT dr = { pad, S(194), w - pad, S(210) };
        DrawLabel(dc, g_fonts.label, kMuted, DetailsLine().c_str(), dr, DT_LEFT | DT_VCENTER, Tracking());
        WString diag = DiagnosticsText();
        r.top = S(216);
        if (!diag.empty())
            DrawLabel(dc, g_fonts.body, kWarn, diag.c_str(), r, DT_LEFT | DT_TOP | DT_WORDBREAK);
    }

    r = { pad, S(264), w - pad, S(280) };
    DrawLabel(dc, g_fonts.label, kFaint, L"TRANSLATED BY", r, DT_LEFT | DT_VCENTER, Tracking());
    r = { pad, S(284), w - pad, S(330) };
    WString credit = s.loaded && !s.table.contributors.empty()
                         ? s.table.contributors + L", for NCS Dummy by revtor."
                         : WString(L"revtor and contributors, for NCS Dummy.");
    for (size_t i = 0; i + 1 < credit.size(); ++i) {
        if (credit[i] == L',' && credit[i + 1] != L' ')
            credit.insert(i + 1, L" ");  // "revtor,IcemanBHE" -> "revtor, IcemanBHE"
    }
    DrawLabel(dc, g_fonts.body, kSecondary, credit.c_str(), r, DT_LEFT | DT_TOP | DT_WORDBREAK);

    rule = { pad, S(346), w - pad, S(347) };
    Fill(dc, rule, kRule);
    RECT dl;
    GetWindowRect(d->chooseDummy, &dl);
    MapWindowPoints(NULL, d->hwnd, (POINT*)&dl, 2);
    r = { pad, S(362), w - pad, S(378) };
    DrawLabel(dc, g_fonts.label, kFaint, L"NCS DUMMY / TRANSLATIONS.CSV IS LOOKED FOR NEXT TO IT", r,
              DT_LEFT | DT_VCENTER, Tracking());
    r = { pad, S(382), dl.left - S(16), S(404) };
    WString dummy = ReadSetting(L"NcsDummyPath");
    if (dummy.empty())
        DrawLabel(dc, g_fonts.body, kSecondary, L"Not set.", r, DT_LEFT | DT_VCENTER);
    else
        DrawLabel(dc, g_fonts.body, kWhite, dummy.c_str(), r, DT_LEFT | DT_VCENTER | DT_PATH_ELLIPSIS);

    r = { pad, S(494), w - pad, S(510) };
    DrawLabel(dc, g_fonts.label, kFaint, L"ORDER", r, DT_LEFT | DT_VCENTER, Tracking());

    int by = client.bottom - pad - S(44);
    RECT note = { pad, by - S(68), w - pad, by - S(16) };
    DashedFrame(dc, note, kBorder);
    RECT nt = { note.left + S(14), note.top + S(8), note.right - S(14), note.bottom - S(6) };
    DrawLabel(dc, g_fonts.body, kSecondary,
              L"The translations are general names for each keyword. They don't say what a function does in a "
              L"particular ECU.",
              nt, DT_LEFT | DT_TOP | DT_WORDBREAK);
}

static bool PickFile(HWND owner, const wchar_t* title, const wchar_t* filter, WString* path)
{
    wchar_t file[MAX_PATH];
    lstrcpynW(file, path->c_str(), MAX_PATH);
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn))
        return false;
    *path = file;
    return true;
}

static void Reload(TranslationsDialog* d)
{
    HCURSOR old = SetCursor(LoadCursorW(NULL, IDC_WAIT));
    LoadTranslations();
    SetCursor(old);
    UpdateControls(d);
}

static void CreateDialogControls(TranslationsDialog* d)
{
    int pad = S(24), w = S(kWidth), h = S(kHeight);
    d->chooseFile = CreateThemedButton(d->hwnd, IDC_CHOOSE_FILE, L"CHOOSE FILE", BUTTON_LINK, kPage);
    d->useDummy = CreateThemedButton(d->hwnd, IDC_USE_DUMMY, L"USE NCS DUMMY'S", BUTTON_LINK, kPage);
    d->chooseDummy = CreateThemedButton(d->hwnd, IDC_CHOOSE_DUMMY, L"CHOOSE", BUTTON_LINK, kPage);
    d->show = CreateThemedButton(d->hwnd, IDC_SHOW, L"Show English names next to keywords", BUTTON_CHECK, kPage);
    d->autoFind = CreateThemedButton(d->hwnd, IDC_AUTO, L"Find Translations.csv next to NCS Dummy", BUTTON_CHECK,
                                     kPage);
    d->keywordFirst =
        CreateThemedButton(d->hwnd, IDC_KEYWORD_FIRST, L"Keyword first, English name below", BUTTON_CHECK, kPage);
    d->translationFirst = CreateThemedButton(d->hwnd, IDC_TRANSLATION_FIRST, L"English name first, keyword below",
                                             BUTTON_CHECK, kPage);
    RECT r = { pad - S(2), S(424), w - pad, S(448) };
    Place(d->show, r);
    r = { pad - S(2), S(454), w - pad, S(478) };
    Place(d->autoFind, r);
    r = { pad - S(2), S(516), w / 2, S(540) };
    Place(d->keywordFirst, r);
    r = { w / 2, S(516), w - pad, S(540) };
    Place(d->translationFirst, r);
    int by = h - pad - S(44);
    d->close = CreateThemedButton(d->hwnd, IDCANCEL, L"CLOSE", BUTTON_PRIMARY, kPage);
    d->reload = CreateThemedButton(d->hwnd, IDC_RELOAD, L"RELOAD", BUTTON_SECONDARY, kPage);
    r = { w - pad - S(130), by, w - pad, by + S(44) };
    Place(d->close, r);
    r = { r.left - S(8) - S(130), by, r.left - S(8), by + S(44) };
    Place(d->reload, r);
    UpdateControls(d);
}

static void OnCommand(TranslationsDialog* d, int id)
{
    switch (id) {
    case IDC_CHOOSE_FILE: {
        WString path = ReadSetting(L"TranslationsPath");
        if (path.empty())
            path = Translations().path;
        if (PickFile(d->hwnd, L"Choose a translation file", L"Translation files (*.csv)\0*.csv\0All files\0*.*\0",
                     &path)) {
            WriteSetting(L"TranslationsPath", path);
            Reload(d);
        }
        break;
    }
    case IDC_USE_DUMMY:
        WriteSetting(L"TranslationsPath", L"");
        Reload(d);
        SetFocus(d->chooseFile);
        break;
    case IDC_CHOOSE_DUMMY: {
        WString path = ReadSetting(L"NcsDummyPath");
        if (PickFile(d->hwnd, L"Choose NCS Dummy", L"Programs (*.exe)\0*.exe\0All files\0*.*\0", &path)) {
            WriteSetting(L"NcsDummyPath", path);
            Reload(d);
        }
        break;
    }
    case IDC_SHOW:
        SetTranslationsShown(!IsThemedChecked(d->show));
        UpdateControls(d);
        break;
    case IDC_AUTO:
        WriteSetting(L"TranslationsAuto", IsThemedChecked(d->autoFind) ? L"0" : L"1");
        Reload(d);
        break;
    case IDC_KEYWORD_FIRST:
    case IDC_TRANSLATION_FIRST:
        SetTranslationFirst(id == IDC_TRANSLATION_FIRST);
        UpdateControls(d);
        break;
    case IDC_RELOAD:
        Reload(d);
        break;
    }
}

static LRESULT CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    TranslationsDialog* d = (TranslationsDialog*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_NCCREATE:
        d = (TranslationsDialog*)((CREATESTRUCTW*)lParam)->lpCreateParams;
        d->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)d);
        break;
    case WM_CREATE:
        CreateDialogControls(d);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        g_painting = d;
        PaintBuffered(hwnd, PaintDialog);
        return 0;
    case WM_DRAWITEM:
        DrawThemedButton((const DRAWITEMSTRUCT*)lParam);
        return TRUE;
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDOK) {  // Enter presses the focused button
            HWND focus = GetFocus();
            if (focus && IsWindowEnabled(focus) && GetParent(focus) == hwnd)
                SendMessageW(focus, BM_CLICK, 0, 0);
            return 0;
        }
        if (id == IDCANCEL) {
            d->done = true;
            return 0;
        }
        OnCommand(d, id);
        return 0;
    }
    case WM_CLOSE:
        d->done = true;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool RunTranslationsDialog(HWND owner)
{
    unsigned before = TranslationsGeneration();
    TranslationsDialog d = {};
    HWND hwnd = CreateModal(owner, kDialogClass, DialogProc, L"Translations", S(kWidth), S(kHeight), &d);
    if (!hwnd)
        return false;
    RunModal(owner, hwnd, d.close, &d.done);
    return TranslationsGeneration() != before;
}
