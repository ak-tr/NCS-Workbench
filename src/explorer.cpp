// Parameter Explorer. See explorer.h.
//
// Picking a chassis indexes its DATEN folder: every module file (*.Cxx) is
// read once into a ParamIndex (paramindex.h), a few files per timer tick so
// the window stays responsive; WM_TIMER comes after input and painting.
// Searching then only looks at the index. The index is rebuilt when the
// folder's files change and dropped when the window closes.

#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <vector>

#include "daten.h"
#include "explorer.h"
#include "modal.h"
#include "sessiondlg.h"
#include "theme.h"
#include "transdlg.h"
#include "translator.h"
#include "util.h"

static const wchar_t kDialogClass[] = L"NcsWorkbenchExplorer";

enum {
    IDC_CHASSIS = 100,
    IDC_SEARCH,
    IDC_RESULTS,
    IDC_WHERE,
    IDC_VALUES,
    IDC_COPY_KEYWORD,
    IDC_COPY_MODULE,
    IDC_TRANSLATIONS,
    IDC_REINDEX,
    IDC_PROBLEMS,
};

enum { TIMER_INDEX = 1 };
static const UINT WM_APP_CHASSIS = WM_APP + 1;
static const UINT kSliceMs = 30;  // reading per timer tick

struct ExplorerDialog {
    HWND hwnd;
    HWND chassis, search, results, where, values, copyKeyword, copyModule, translations, reindex, problems, close;
    HIMAGELIST resultHeight, whereHeight, valueHeight;

    WString datenDir;  // "" if SP-Daten isn't installed
    WString expectedDaten;  // where it was looked for
    std::vector<std::pair<WString, WString> > chassisList;  // (chassis, DATEN folder)
    WString folder;  // DATEN folder of the picked chassis; "" if none
    bool unknownChassis;
    bool chassisEdited;  // the user changed the chassis (then it's remembered)

    // Index of `folder`
    bool indexing, indexed, beginFailed;
    IndexJob job;
    ParamIndex index;
    String signature;
    std::vector<WString> folded;  // folded translations, by function
    unsigned foldedFor;           // TranslationsGeneration() they were made for

    std::vector<SearchHit> hits;
    int selected;       // index into hits, -1 if none
    int selectedWhere;  // index into the function's occurrences
    WString note;       // "COPIED ..." until the next change
    bool done;
};

static ExplorerDialog* g_painting;  // PaintBuffered takes a plain function

static WString IntText(size_t n)
{
    return FormatThousands(n);
}

static WString HexId(unsigned id, int digits)
{
    wchar_t buf[16];
    wsprintfW(buf, digits == 2 ? L"%02X" : L"%04X", id);
    return buf;
}

static const IndexedFunction* SelectedFunction(ExplorerDialog* d)
{
    if (d->selected < 0 || d->selected >= (int)d->hits.size())
        return NULL;
    return &d->index.functions[d->hits[d->selected].function];
}

static const FunctionOccurrence* SelectedOccurrence(ExplorerDialog* d)
{
    const IndexedFunction* f = SelectedFunction(d);
    if (!f || d->selectedWhere < 0 || d->selectedWhere >= (int)f->occurrences.size())
        return NULL;
    return &f->occurrences[d->selectedWhere];
}

// ---------------------------------------------------------------------------
// Layout

struct Rects {
    RECT status, chassisLabel, chassis, chassisInfo, searchLabel, searchBox, search, searchInfo;
    RECT results, detail, keyword, translation, meta, whereLabel, where, valuesLabel, values;
    RECT close, copyModule, copyKeyword;
};

static int g_translationHeight;  // measured for the selected function, see MeasureTranslation

static Rects ComputeRects(int w, int h)
{
    int pad = S(24);
    Rects r;
    r.status = { pad, S(84), w - pad, S(100) };
    int fieldWidth = S(200);
    r.chassisLabel = { pad, S(116), pad + fieldWidth, S(132) };
    r.chassis = { pad, S(136), pad + fieldWidth, S(136) + S(300) };  // includes the drop-down
    r.chassisInfo = { pad, S(178), pad + fieldWidth, S(194) };
    int sl = pad + fieldWidth + S(16);
    r.searchLabel = { sl, S(116), w - pad, S(132) };
    r.searchBox = { sl, S(136), w - pad, S(170) };
    r.search = { sl + S(10), S(144), w - pad - S(10), S(163) };
    r.searchInfo = { sl, S(178), w - pad, S(194) };

    int by = h - pad - S(44);
    r.close = { w - pad - S(120), by, w - pad, by + S(44) };
    r.copyModule = { r.close.left - S(8) - S(170), by, r.close.left - S(8), by + S(44) };
    r.copyKeyword = { r.copyModule.left - S(8) - S(170), by, r.copyModule.left - S(8), by + S(44) };

    int top = S(212), bottom = by - S(16);
    r.results = { pad, top, pad + (w - 2 * pad) * 44 / 100, bottom };
    r.detail = { r.results.right + S(24), top, w - pad, bottom };
    const RECT& dt = r.detail;
    r.keyword = { dt.left, dt.top + S(20), dt.right, dt.top + S(46) };
    int th = g_translationHeight ? g_translationHeight : S(20);
    r.translation = { dt.left, dt.top + S(52), dt.right, dt.top + S(52) + th };
    r.meta = { dt.left, r.translation.bottom + S(8), dt.right, r.translation.bottom + S(24) };
    int listsTop = r.meta.bottom + S(16);
    int space = std::max(0, (int)(dt.bottom - listsTop) - S(2 * 22));
    int whereHeight = space * 2 / 5;
    r.whereLabel = { dt.left, listsTop, dt.right, listsTop + S(16) };
    r.where = { dt.left, listsTop + S(22), dt.right, listsTop + S(22) + whereHeight };
    r.valuesLabel = { dt.left, r.where.bottom + S(14), dt.right, r.where.bottom + S(30) };
    r.values = { dt.left, r.where.bottom + S(36), dt.right, dt.bottom };
    return r;
}

// The selected function's translation, word-wrapped: as many lines as it needs, up to five.
static void MeasureTranslation(ExplorerDialog* d)
{
    RECT client;
    GetClientRect(d->hwnd, &client);
    g_translationHeight = 0;
    Rects r = ComputeRects(client.right, client.bottom);
    const IndexedFunction* f = SelectedFunction(d);
    const WString* t = f ? Translate(f->keyword) : NULL;
    int h = S(20);
    if (t) {
        HDC dc = GetDC(d->hwnd);
        HGDIOBJ old = SelectObject(dc, g_fonts.body);
        RECT calc = { 0, 0, r.translation.right - r.translation.left, 0 };
        DrawTextW(dc, t->c_str(), -1, &calc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, old);
        ReleaseDC(d->hwnd, dc);
        h = std::min(std::max((int)calc.bottom, S(20)), S(20) * 5);
    }
    g_translationHeight = h;
}

static void LayoutDialog(ExplorerDialog* d)
{
    RECT client;
    GetClientRect(d->hwnd, &client);
    MeasureTranslation(d);
    Rects r = ComputeRects(client.right, client.bottom);
    Place(d->chassis, r.chassis);
    // Moving a combo box selects its text; only show that while it has focus.
    HWND focus = GetFocus();
    if (focus != d->chassis && !IsChild(d->chassis, focus))
        SendMessageW(d->chassis, CB_SETEDITSEL, 0, MAKELPARAM(-1, 0));
    Place(d->search, r.search);
    Place(d->results, r.results);
    Place(d->where, r.where);
    Place(d->values, r.values);
    Place(d->close, r.close);
    Place(d->copyModule, r.copyModule);
    Place(d->copyKeyword, r.copyKeyword);
    int pad = S(24);
    Place(d->translations, RightLink(d->translations, client.right - pad, S(32)));
    int cy = (r.status.top + r.status.bottom) / 2;
    RECT reindex = RightLink(d->reindex, r.status.right, cy);
    Place(d->reindex, reindex);
    Place(d->problems, RightLink(d->problems, reindex.left - S(20), cy));
    FitListColumn(d->results);
    FitListColumn(d->where);
    FitListColumn(d->values);
    InvalidateRect(d->hwnd, NULL, FALSE);
}

// ---------------------------------------------------------------------------
// Search and selection

static void UpdateDetail(ExplorerDialog* d)
{
    const IndexedFunction* f = SelectedFunction(d);
    int where = f ? (int)f->occurrences.size() : 0;
    ListView_SetItemCountEx(d->where, where, 0);
    if (f && d->selectedWhere >= 0)
        ListView_SetItemState(d->where, d->selectedWhere, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    const FunctionOccurrence* o = SelectedOccurrence(d);
    ListView_SetItemCountEx(d->values, o ? (int)o->values.size() : 0, 0);
    ShowWindow(d->where, where ? SW_SHOW : SW_HIDE);
    ShowWindow(d->values, o && !o->values.empty() ? SW_SHOW : SW_HIDE);
    EnableWindow(d->copyKeyword, f != NULL);
    EnableWindow(d->copyModule, o != NULL);
    LayoutDialog(d);  // the translation's height may have changed
    InvalidateRect(d->where, NULL, TRUE);
    InvalidateRect(d->values, NULL, TRUE);
}

static void SelectHit(ExplorerDialog* d, int hit)
{
    d->selected = hit >= 0 && hit < (int)d->hits.size() ? hit : -1;
    d->selectedWhere = d->selected >= 0 ? 0 : -1;
    d->note.clear();
    UpdateDetail(d);
}

static void RefreshTranslations(ExplorerDialog* d)
{
    if (d->foldedFor == TranslationsGeneration() && d->folded.size() == d->index.functions.size())
        return;
    d->folded.assign(d->index.functions.size(), WString());
    for (size_t i = 0; i < d->index.named; ++i) {
        const WString* t = Translate(d->index.functions[i].keyword);
        if (t)
            d->folded[i] = FoldString(*t);
    }
    d->foldedFor = TranslationsGeneration();
}

static void RunSearch(ExplorerDialog* d)
{
    unsigned keep = 0;
    bool hadSelection = SelectedFunction(d) != NULL;
    if (hadSelection)
        keep = SelectedFunction(d)->id;
    d->hits.clear();
    if (d->indexed) {
        RefreshTranslations(d);
        d->hits = SearchIndex(d->index, GetWindowTextString(d->search), d->folded);
    }
    int select = d->hits.empty() ? -1 : 0;
    for (size_t i = 0; hadSelection && i < d->hits.size(); ++i) {
        if (d->index.functions[d->hits[i].function].id == keep)
            select = (int)i;
    }
    ListView_SetItemCountEx(d->results, (int)d->hits.size(), 0);
    ShowWindow(d->results, d->hits.empty() ? SW_HIDE : SW_SHOW);
    FitListColumn(d->results);
    if (select >= 0) {
        ListView_SetItemState(d->results, select, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(d->results, select, FALSE);
    }
    SelectHit(d, select);
    InvalidateRect(d->results, NULL, TRUE);
    InvalidateRect(d->hwnd, NULL, FALSE);
}

// ---------------------------------------------------------------------------
// Indexing

static void StopIndexing(ExplorerDialog* d)
{
    if (!d->indexing)
        return;
    KillTimer(d->hwnd, TIMER_INDEX);
    d->indexing = false;
}

static void ShowIndexState(ExplorerDialog* d)
{
    SetWindowTextW(d->reindex, d->indexing ? L"STOP" : L"INDEX AGAIN");
    ShowWindow(d->reindex, d->folder.empty() || d->datenDir.empty() ? SW_HIDE : SW_SHOW);
    ShowWindow(d->problems, !d->indexing && d->indexed && !d->index.problems.empty() ? SW_SHOW : SW_HIDE);
    LayoutDialog(d);
}

static void StartIndexing(ExplorerDialog* d)
{
    StopIndexing(d);
    d->indexed = false;
    d->beginFailed = false;
    d->index = ParamIndex();
    d->folded.clear();
    d->hits.clear();
    if (d->folder.empty()) {
        RunSearch(d);
        ShowIndexState(d);
        return;
    }
    WString path = JoinPath(d->datenDir, d->folder);
    d->signature = FolderSignature(path);
    if (!BeginIndexJob(path, &d->job)) {
        d->beginFailed = true;
    } else {
        d->indexing = true;
        SetTimer(d->hwnd, TIMER_INDEX, 10, NULL);
    }
    RunSearch(d);
    ShowIndexState(d);
}

static void FinishIndexing(ExplorerDialog* d, bool cancelled)
{
    StopIndexing(d);
    d->index = FinishIndexJob(&d->job, cancelled);
    d->job = IndexJob();  // the per-file data isn't needed any more
    d->indexed = true;
    d->foldedFor = 0;
    RunSearch(d);
    ShowIndexState(d);
}

static void OnIndexTimer(ExplorerDialog* d)
{
    if (!d->indexing)
        return;
    DWORD start = GetTickCount();
    bool more = true;
    while (more && GetTickCount() - start < kSliceMs)
        more = StepIndexJob(&d->job);
    if (!more)
        FinishIndexing(d, false);
    else
        InvalidateRect(d->hwnd, NULL, FALSE);  // progress
}

// picked: the user chose it (remembered), rather than the window opening with it.
static void UpdateForChassis(ExplorerDialog* d, bool picked)
{
    WString typed = GetWindowTextString(d->chassis);
    WString folder = FolderForChassis(d->chassisList, typed);
    d->unknownChassis = folder.empty() && !typed.empty();
    if (picked && !folder.empty() && lstrcmpiW(ReadSetting(L"ExplorerChassis").c_str(), typed.c_str()) != 0)
        WriteSetting(L"ExplorerChassis", typed);
    if (lstrcmpiW(folder.c_str(), d->folder.c_str()) == 0 && (d->indexed || d->indexing || d->beginFailed)) {
        InvalidateRect(d->hwnd, NULL, FALSE);
        return;  // E90 -> E91: same DATEN folder, same index
    }
    d->folder = folder;
    StartIndexing(d);
}

// After activation: rebuild if the folder's files changed meanwhile.
static void CheckForChanges(ExplorerDialog* d)
{
    if (!d->indexed || d->folder.empty())
        return;
    if (FolderSignature(JoinPath(d->datenDir, d->folder)) != d->signature)
        StartIndexing(d);
}

// ---------------------------------------------------------------------------
// Painting

static WString StatusText(ExplorerDialog* d, COLORREF* color)
{
    *color = kWarn;
    if (d->datenDir.empty())
        return L"SP-DATEN NOT FOUND";
    if (d->chassisList.empty())
        return L"NO CHASSIS FOLDER WITH MODULE FILES (*.CXX) IN " + d->datenDir;
    if (d->folder.empty())
        return d->unknownChassis ? L"THIS CHASSIS ISN'T IN SP-DATEN / PICK ONE FROM THE LIST" : L"PICK A CHASSIS";
    WString where = L"DATEN\\" + d->folder;
    if (d->beginFailed) {
        *color = kBad;
        return L"COULDN'T READ " + where;
    }
    if (d->indexing) {
        *color = kAccent;
        return L"INDEXING " + where + L" / " + IntText(d->job.next) + L" OF " + IntText(d->job.files.size()) +
               L" MODULE FILES";
    }
    if (!d->indexed)
        return WString();
    const ParamIndex& x = d->index;
    if (!d->note.empty()) {
        *color = kGood;
        return d->note;
    }
    if (x.moduleFiles == 0)
        return L"NO MODULE FILES (*.CXX) IN " + where;
    if (!x.functionNames) {
        *color = kBad;
        return L"NO SWTFSW*.DAT IN " + where + L" / FUNCTIONS CAN'T BE NAMED OR SEARCHED";
    }
    int read = (int)x.modules.size();
    if (x.cancelled)
        return L"STOPPED AFTER " + IntText(read + x.problems.size()) + L" OF " + IntText(x.moduleFiles) +
               L" MODULE FILES / RESULTS ARE INCOMPLETE";
    if (!x.problems.empty())
        return IntText(x.problems.size()) + L" OF " + IntText(x.moduleFiles) +
               L" MODULE FILES COULDN'T BE SEARCHED / RESULTS MAY BE INCOMPLETE";
    *color = kMuted;
    WString text = where + L" / " + IntText(read) + L" MODULE FILES / " + IntText(x.named) + L" FUNCTIONS";
    if (!x.valueNamesRead)
        text += L" / NO SWTPSW*.DAT: VALUES UNNAMED";
    return text;
}

static void PaintResultsMessage(HDC dc, ExplorerDialog* d, const RECT& area)
{
    Fill(dc, area, kTile3);
    RECT m = { area.left + S(16), area.top + S(20), area.right - S(16), area.top + S(46) };
    RECT sub = { m.left, m.bottom + S(6), m.right, area.bottom - S(16) };
    if (!d->indexed) {
        if (d->indexing)
            DrawLabel(dc, g_fonts.body, kSecondary, L"Reading the module files...", m, DT_LEFT | DT_VCENTER);
        return;
    }
    const ParamIndex& x = d->index;
    if (!x.functionNames || x.named == 0) {
        DrawLabel(dc, g_fonts.title, kWhite, L"Nothing to search.", m, DT_LEFT | DT_VCENTER);
        return;
    }
    DrawLabel(dc, g_fonts.title, kWhite, L"No matching functions.", m, DT_LEFT | DT_VCENTER);
    WString query = TrimSpaces(GetWindowTextString(d->search));
    WString text;
    bool incomplete = x.cancelled || !x.problems.empty();
    // A keyword SWTFSW knows but no module codes: say so, rather than just "not found".
    bool known = false;
    for (size_t i = 0; i < x.otherKeywords.size() && !known; ++i)
        known = lstrcmpiW(Widen(x.otherKeywords[i]).c_str(), query.c_str()) == 0;
    if (known)
        text = query + L" is a function name in SP-Daten's keyword list (SWTFSW), but no module file " +
               (incomplete ? WString(L"that could be read ") : WString()) + L"in DATEN\\" + d->folder + L" codes it.";
    else
        text = L"No module file " + (incomplete ? WString(L"that could be read ") : WString()) + L"in DATEN\\" +
               d->folder + L" has a function " + (TranslationsShown() ? L"named or translated" : L"named") +
               L" like this.";
    if (incomplete)
        text += L" Some module files couldn't be searched, so it may still be in one of them.";
    DrawLabel(dc, g_fonts.body, kSecondary, text.c_str(), sub, DT_LEFT | DT_TOP | DT_WORDBREAK);
}

static void PaintDetail(HDC dc, ExplorerDialog* d, const Rects& r)
{
    const IndexedFunction* f = SelectedFunction(d);
    const RECT& dt = r.detail;
    if (!f)
        return;
    RECT t = { dt.left, dt.top, dt.right, dt.top + S(16) };
    DrawLabel(dc, g_fonts.label, kFaint, L"FUNCTION (FSW)", t, DT_LEFT | DT_VCENTER, Tracking());
    DrawLabel(dc, g_fonts.monoTitle, kWhite, Widen(f->keyword).c_str(), r.keyword, DT_LEFT | DT_VCENTER);
    const WString* tr = Translate(f->keyword);
    if (tr)
        DrawLabel(dc, g_fonts.body, kText, tr->c_str(), r.translation, DT_LEFT | DT_TOP | DT_WORDBREAK);
    else
        DrawLabel(dc, g_fonts.body, kSecondary,
                  !Translations().loaded ? L"Translations aren't loaded."
                  : !TranslationsShown() ? L"Translations are hidden."
                                         : L"No English name in NCS Dummy's translations.",
                  r.translation, DT_LEFT | DT_TOP);
    WString meta = L"DATEN\\" + d->folder + L" / FSW " + HexId(f->id, 4);
    if (tr)
        meta += L" / NAME FROM NCS DUMMY, NOT ECU-SPECIFIC";
    DrawLabel(dc, g_fonts.label, kFaint, meta.c_str(), r.meta, DT_LEFT | DT_VCENTER, Tracking());

    size_t n = f->occurrences.size();
    WString where = L"FOUND IN " + IntText(n) + (n == 1 ? L" MODULE FILE" : L" MODULE FILES");
    DrawLabel(dc, g_fonts.label, kFaint, where.c_str(), r.whereLabel, DT_LEFT | DT_VCENTER, Tracking());

    const FunctionOccurrence* o = SelectedOccurrence(d);
    if (!o)
        return;
    const IndexedModule& m = d->index.modules[o->module];
    WString valuesLabel = L"VALUES (PSW) IN " + Widen(m.file);
    DrawLabel(dc, g_fonts.label, kFaint, valuesLabel.c_str(), r.valuesLabel, DT_LEFT | DT_VCENTER, Tracking());
    if (o->values.empty()) {
        Fill(dc, r.values, kTile3);
        RECT c = { r.values.left + S(14), r.values.top + S(10), r.values.right - S(14), r.values.bottom - S(10) };
        DrawLabel(dc, g_fonts.body, kSecondary,
                  o->direct ? L"This module codes the function with a number rather than a list of values."
                            : L"This module lists no values for the function.",
                  c, DT_LEFT | DT_TOP | DT_WORDBREAK);
    }
}

static void PaintDialog(HDC dc, const RECT& client)
{
    ExplorerDialog* d = g_painting;
    int pad = S(24), w = client.right;
    Rects r = ComputeRects(client.right, client.bottom);
    Fill(dc, client, kPage);

    RECT link;
    GetWindowRect(d->translations, &link);
    MapWindowPoints(NULL, d->hwnd, (POINT*)&link, 2);
    RECT t = { pad, S(24), link.left - S(16), S(40) };
    DrawLabel(dc, g_fonts.label, kFaint, L"TOOLS / READ-ONLY", t, DT_LEFT | DT_VCENTER, Tracking());
    t = { pad, S(44), w - pad, S(74) };
    DrawLabel(dc, g_fonts.heading, kWhite, L"Parameter Explorer", t, DT_LEFT | DT_VCENTER);

    COLORREF color;
    WString status = StatusText(d, &color);
    RECT sr = r.status;
    if (IsWindowVisible(d->problems)) {
        GetWindowRect(d->problems, &link);
        MapWindowPoints(NULL, d->hwnd, (POINT*)&link, 2);
        sr.right = link.left - S(16);
    } else if (IsWindowVisible(d->reindex)) {
        GetWindowRect(d->reindex, &link);
        MapWindowPoints(NULL, d->hwnd, (POINT*)&link, 2);
        sr.right = link.left - S(16);
    }
    DrawLabel(dc, g_fonts.label, color, status.c_str(), sr, DT_LEFT | DT_VCENTER, Tracking());

    DrawLabel(dc, g_fonts.label, kFaint, L"CHASSIS", r.chassisLabel, DT_LEFT | DT_VCENTER, Tracking());
    if (!d->folder.empty()) {
        WString info = L"DATEN\\" + d->folder;
        DrawLabel(dc, g_fonts.label, kMuted, info.c_str(), r.chassisInfo, DT_LEFT | DT_VCENTER, Tracking());
    } else if (d->unknownChassis) {
        DrawLabel(dc, g_fonts.label, kWarn, L"NOT IN SP-DATEN", r.chassisInfo, DT_LEFT | DT_VCENTER, Tracking());
    }
    DrawLabel(dc, g_fonts.label, kFaint, L"SEARCH", r.searchLabel, DT_LEFT | DT_VCENTER, Tracking());
    Fill(dc, r.searchBox, kTile1);
    Frame(dc, r.searchBox, GetFocus() == d->search ? kAccent : kBorder);
    WString hint;
    if (d->indexed && d->index.named) {
        hint = TranslationsShown() ? L"BMW KEYWORD OR ENGLISH NAME / " : L"BMW KEYWORD / ";
        hint += TrimSpaces(GetWindowTextString(d->search)).empty()
                    ? L"ALL " + IntText(d->hits.size()) + L" FUNCTIONS"
                    : IntText(d->hits.size()) + (d->hits.size() == 1 ? L" MATCH" : L" MATCHES");
    } else {
        hint = TranslationsShown() ? L"BMW KEYWORD OR ENGLISH NAME" : L"BMW KEYWORD";
    }
    DrawLabel(dc, g_fonts.label, kMuted, hint.c_str(), r.searchInfo, DT_LEFT | DT_VCENTER, Tracking());

    if (d->hits.empty() && !d->datenDir.empty() && !d->folder.empty() && !d->beginFailed)
        PaintResultsMessage(dc, d, r.results);
    if (d->datenDir.empty()) {
        Fill(dc, r.results, kTile3);
        RECT m = { r.results.left + S(16), r.results.top + S(20), r.results.right - S(16), r.results.bottom - S(16) };
        WString text = L"Workbench reads SP-Daten from the DATEN folder next to the WORK folder, and there is none: " +
                       d->expectedDaten + L". Install SP-Daten there, or change the WORK folder in the main window.";
        DrawLabel(dc, g_fonts.body, kSecondary, text.c_str(), m, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_EDITCONTROL);
    }
    PaintDetail(dc, d, r);
}

static LRESULT ResultCustomDraw(ExplorerDialog* d, NMLVCUSTOMDRAW* cd)
{
    switch (cd->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT: {
        int item = (int)cd->nmcd.dwItemSpec;
        if (item < 0 || item >= (int)d->hits.size())
            break;
        const IndexedFunction& f = d->index.functions[d->hits[item].function];
        HDC dc = cd->nmcd.hdc;
        UINT state = ListView_GetItemState(d->results, item, LVIS_SELECTED | LVIS_FOCUSED);
        RECT rc;
        ListView_GetItemRect(d->results, item, &rc, LVIR_BOUNDS);
        Fill(dc, rc, (state & LVIS_SELECTED) ? kSelect : kTile3);
        RECT rule = { rc.left, rc.bottom - 1, rc.right, rc.bottom };
        Fill(dc, rule, kTile1);
        size_t n = f.occurrences.size();
        WString count = n == 1 ? WString(L"1 MODULE") : IntText(n) + L" MODULES";
        int cw = MeasureLabel(dc, g_fonts.label, count.c_str(), Tracking()) + Tracking();
        RECT c = { rc.right - S(12) - cw, rc.top, rc.right - S(12), rc.bottom };
        DrawLabel(dc, g_fonts.label, kMuted, count.c_str(), c, DT_RIGHT | DT_VCENTER, Tracking());
        c = { rc.left + S(14), rc.top, c.left - S(12), rc.bottom };
        DrawKeyword(dc, f.keyword, c, kWhite);
        if ((state & LVIS_FOCUSED) && GetFocus() == d->results)
            Frame(dc, rc, kAccent);
        return CDRF_SKIPDEFAULT;
    }
    }
    return CDRF_DODEFAULT;
}

static WString ModuleIndexText(const IndexedModule& m)
{
    return m.codingIndex >= 0 ? L"INDEX " + HexId((unsigned)m.codingIndex, 2) : WString(L"NO INDEX");
}

static WString ValuesCountText(const FunctionOccurrence& o)
{
    if (o.values.empty())
        return o.direct ? L"NUMBER" : L"NO VALUES";
    return IntText(o.values.size()) + (o.values.size() == 1 ? L" VALUE" : L" VALUES");
}

static LRESULT WhereCustomDraw(ExplorerDialog* d, NMLVCUSTOMDRAW* cd)
{
    switch (cd->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT: {
        const IndexedFunction* f = SelectedFunction(d);
        int item = (int)cd->nmcd.dwItemSpec;
        if (!f || item < 0 || item >= (int)f->occurrences.size())
            break;
        const FunctionOccurrence& o = f->occurrences[item];
        const IndexedModule& m = d->index.modules[o.module];
        HDC dc = cd->nmcd.hdc;
        UINT state = ListView_GetItemState(d->where, item, LVIS_SELECTED | LVIS_FOCUSED);
        RECT rc;
        ListView_GetItemRect(d->where, item, &rc, LVIR_BOUNDS);
        Fill(dc, rc, (state & LVIS_SELECTED) ? kSelect : kTile3);
        RECT rule = { rc.left, rc.bottom - 1, rc.right, rc.bottom };
        Fill(dc, rule, kTile1);
        int width = rc.right - rc.left;
        int fileLeft = rc.left + S(14) + std::min(S(80), width / 5);
        int indexLeft = rc.left + width * 62 / 100;
        RECT c = { rc.left + S(14), rc.top, fileLeft - S(8), rc.bottom };
        DrawLabel(dc, g_fonts.label, kMuted, m.family.empty() ? L"-" : Widen(m.family).c_str(), c,
                  DT_LEFT | DT_VCENTER, Tracking());
        c = { fileLeft, rc.top, indexLeft - S(8), rc.bottom };
        DrawLabel(dc, g_fonts.mono, kWhite, Widen(m.file).c_str(), c, DT_LEFT | DT_VCENTER);
        WString count = ValuesCountText(o);
        int cw = MeasureLabel(dc, g_fonts.label, count.c_str(), Tracking()) + Tracking();
        RECT cc = { rc.right - S(12) - cw, rc.top, rc.right - S(12), rc.bottom };
        DrawLabel(dc, g_fonts.label, kFaint, count.c_str(), cc, DT_RIGHT | DT_VCENTER, Tracking());
        c = { indexLeft, rc.top, cc.left - S(8), rc.bottom };
        DrawLabel(dc, g_fonts.label, kText, ModuleIndexText(m).c_str(), c, DT_LEFT | DT_VCENTER, Tracking());
        if ((state & LVIS_FOCUSED) && GetFocus() == d->where)
            Frame(dc, rc, kAccent);
        return CDRF_SKIPDEFAULT;
    }
    }
    return CDRF_DODEFAULT;
}

static LRESULT ValueCustomDraw(ExplorerDialog* d, NMLVCUSTOMDRAW* cd)
{
    switch (cd->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT: {
        const FunctionOccurrence* o = SelectedOccurrence(d);
        int item = (int)cd->nmcd.dwItemSpec;
        if (!o || item < 0 || item >= (int)o->values.size())
            break;
        unsigned id = o->values[item];
        const String* name = ValueName(d->index, id);
        HDC dc = cd->nmcd.hdc;
        UINT state = ListView_GetItemState(d->values, item, LVIS_SELECTED | LVIS_FOCUSED);
        RECT rc;
        ListView_GetItemRect(d->values, item, &rc, LVIR_BOUNDS);
        Fill(dc, rc, (state & LVIS_SELECTED) ? kSelect : kTile3);
        RECT rule = { rc.left, rc.bottom - 1, rc.right, rc.bottom };
        Fill(dc, rule, kTile1);
        RECT c = { rc.left + S(14), rc.top, rc.right - S(12), rc.bottom };
        if (name)
            DrawKeyword(dc, *name, c, kText);
        else
            DrawLabel(dc, g_fonts.label, kWarn, (L"PSW " + HexId(id, 4) + L" / NOT IN SWTPSW").c_str(), c,
                      DT_LEFT | DT_VCENTER, Tracking());
        if ((state & LVIS_FOCUSED) && GetFocus() == d->values)
            Frame(dc, rc, kAccent);
        return CDRF_SKIPDEFAULT;
    }
    }
    return CDRF_DODEFAULT;
}

// What screen readers announce for a row (rows are painted by hand).
static WString SpokenRow(ExplorerDialog* d, UINT_PTR list, int item)
{
    if (list == IDC_RESULTS && item >= 0 && item < (int)d->hits.size()) {
        const IndexedFunction& f = d->index.functions[d->hits[item].function];
        return SpokenKeyword(f.keyword) + L", in " + IntText(f.occurrences.size()) + L" module files";
    }
    const IndexedFunction* f = SelectedFunction(d);
    if (list == IDC_WHERE && f && item >= 0 && item < (int)f->occurrences.size()) {
        const FunctionOccurrence& o = f->occurrences[item];
        const IndexedModule& m = d->index.modules[o.module];
        return Widen(m.family) + L" " + Widen(m.file) + L", " + ModuleIndexText(m) + L", " + ValuesCountText(o);
    }
    const FunctionOccurrence* o = SelectedOccurrence(d);
    if (list == IDC_VALUES && o && item >= 0 && item < (int)o->values.size()) {
        const String* name = ValueName(d->index, o->values[item]);
        return name ? SpokenKeyword(*name) : L"PSW " + HexId(o->values[item], 4);
    }
    return WString();
}

// ---------------------------------------------------------------------------
// Window

static void CreateDialogControls(ExplorerDialog* d)
{
    d->chassis = CreateWindowExW(0, L"COMBOBOX", L"",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWN | CBS_AUTOHSCROLL |
                                     CBS_UPPERCASE,
                                 0, 0, 0, 0, d->hwnd, (HMENU)(INT_PTR)IDC_CHASSIS, GetModuleHandleW(NULL), NULL);
    SendMessageW(d->chassis, WM_SETFONT, (WPARAM)g_fonts.mono, FALSE);
    SendMessageW(d->chassis, CB_LIMITTEXT, 20, 0);
    SendMessageW(d->chassis, CB_SETITEMHEIGHT, (WPARAM)-1, S(30));
    SendMessageW(d->chassis, CB_SETITEMHEIGHT, 0, S(24));
    ApplyThemedControl(d->chassis, L"DarkMode_CFD");
    COMBOBOXINFO info = {};
    info.cbSize = sizeof(info);
    if (GetComboBoxInfo(d->chassis, &info))
        ApplyThemedControl(info.hwndList, L"DarkMode_Explorer");
    for (size_t i = 0; i < d->chassisList.size(); ++i)
        SendMessageW(d->chassis, CB_ADDSTRING, 0, (LPARAM)d->chassisList[i].first.c_str());

    d->search = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0,
                                d->hwnd, (HMENU)(INT_PTR)IDC_SEARCH, GetModuleHandleW(NULL), NULL);
    SendMessageW(d->search, WM_SETFONT, (WPARAM)g_fonts.mono, FALSE);
    SendMessageW(d->search, EM_LIMITTEXT, 80, 0);

    d->results = CreateThemedList(d->hwnd, IDC_RESULTS, kTile3, S(46), &d->resultHeight);
    d->where = CreateThemedList(d->hwnd, IDC_WHERE, kTile3, S(30), &d->whereHeight);
    d->values = CreateThemedList(d->hwnd, IDC_VALUES, kTile3, S(42), &d->valueHeight);
    d->translations = CreateThemedButton(d->hwnd, IDC_TRANSLATIONS, TranslationsLinkText().c_str(), BUTTON_LINK, kPage);
    d->reindex = CreateThemedButton(d->hwnd, IDC_REINDEX, L"INDEX AGAIN", BUTTON_LINK, kPage);
    d->problems = CreateThemedButton(d->hwnd, IDC_PROBLEMS, L"WHICH FILES", BUTTON_LINK, kPage);
    d->copyKeyword = CreateThemedButton(d->hwnd, IDC_COPY_KEYWORD, L"COPY KEYWORD", BUTTON_SECONDARY, kPage);
    d->copyModule = CreateThemedButton(d->hwnd, IDC_COPY_MODULE, L"COPY MODULE", BUTTON_SECONDARY, kPage);
    d->close = CreateThemedButton(d->hwnd, IDCANCEL, L"CLOSE", BUTTON_PRIMARY, kPage);

    bool daten = !d->datenDir.empty() && !d->chassisList.empty();
    EnableWindow(d->chassis, daten);
    EnableWindow(d->search, daten);

    // The chassis picked here last time, else the one from the last backup.
    WString chassis = ReadSetting(L"ExplorerChassis");
    if (FolderForChassis(d->chassisList, chassis).empty())
        chassis = ReadSetting(L"Chassis");
    // Else the first chassis named like its folder (E89 rather than E81 for DATEN\E89).
    for (size_t i = 0; i < d->chassisList.size() && FolderForChassis(d->chassisList, chassis).empty(); ++i) {
        if (lstrcmpiW(d->chassisList[i].first.c_str(), d->chassisList[i].second.c_str()) == 0)
            chassis = d->chassisList[i].first;
    }
    if (FolderForChassis(d->chassisList, chassis).empty() && !d->chassisList.empty())
        chassis = d->chassisList[0].first;
    SetWindowTextW(d->chassis, chassis.c_str());
    SendMessageW(d->chassis, CB_SETEDITSEL, 0, MAKELPARAM(-1, 0));  // focus starts in the search field
    LayoutDialog(d);
    UpdateForChassis(d, false);
}

static void ShowProblems(ExplorerDialog* d)
{
    const std::vector<ModuleProblem>& p = d->index.problems;
    WString body;
    size_t shown = std::min(p.size(), (size_t)12);
    for (size_t i = 0; i < shown; ++i) {
        const wchar_t* why = p[i].status == MODULE_OK          ? L"couldn't be read"
                             : p[i].status == MODULE_NOT_NCS    ? L"isn't an SP-Daten file (damaged?)"
                             : p[i].status == MODULE_UNSUPPORTED ? L"is in a format Workbench doesn't know"
                                                                 : L"has no function table";
        body += Widen(p[i].file) + L" " + why + L".\n";
    }
    if (p.size() > shown)
        body += L"And " + IntText(p.size() - shown) + L" more.\n";
    body += L"\nFunctions in these files don't show in the results.";
    RunMessageDialog(d->hwnd, L"Parameter Explorer", L"DATEN\\" + d->folder, L"Module files not searched", body);
}

static void Copy(ExplorerDialog* d, bool module)
{
    const IndexedFunction* f = SelectedFunction(d);
    const FunctionOccurrence* o = SelectedOccurrence(d);
    if (!f || (module && !o))
        return;
    WString text = module ? Widen(d->index.modules[o->module].file) : Widen(f->keyword);
    CopyToClipboard(d->hwnd, text);
    d->note = L"COPIED " + text;
    InvalidateRect(d->hwnd, NULL, FALSE);
}

static LRESULT CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    ExplorerDialog* d = (ExplorerDialog*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_NCCREATE:
        d = (ExplorerDialog*)((CREATESTRUCTW*)lParam)->lpCreateParams;
        d->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)d);
        break;
    case WM_CREATE:
        CreateDialogControls(d);
        return 0;
    case WM_SIZE:
        if (d && d->values)
            LayoutDialog(d);
        return 0;
    case WM_GETMINMAXINFO: {
        SIZE min = ModalWindowSize(S(820), S(600), true);
        ((MINMAXINFO*)lParam)->ptMinTrackSize.x = min.cx;
        ((MINMAXINFO*)lParam)->ptMinTrackSize.y = min.cy;
        return 0;
    }
    case WM_ACTIVATE:
        if (d && LOWORD(wParam) != WA_INACTIVE && !HIWORD(wParam))
            CheckForChanges(d);
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        g_painting = d;
        PaintBuffered(hwnd, PaintDialog);
        return 0;
    case WM_DRAWITEM:
        DrawThemedButton((const DRAWITEMSTRUCT*)lParam);
        return TRUE;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORSTATIC:  // the fields while disabled (no SP-Daten)
        SetTextColor((HDC)wParam, msg == WM_CTLCOLORSTATIC ? kDisabled : kWhite);
        SetBkColor((HDC)wParam, kTile1);
        return (LRESULT)g_fieldBrush;
    case WM_TIMER:
        if (wParam == TIMER_INDEX)
            OnIndexTimer(d);
        return 0;
    case WM_APP_CHASSIS:
        UpdateForChassis(d, true);
        return 0;
    case WM_NOTIFY: {
        NMHDR* nm = (NMHDR*)lParam;
        if (nm->code == NM_CUSTOMDRAW) {
            if (nm->idFrom == IDC_RESULTS)
                return ResultCustomDraw(d, (NMLVCUSTOMDRAW*)lParam);
            if (nm->idFrom == IDC_WHERE)
                return WhereCustomDraw(d, (NMLVCUSTOMDRAW*)lParam);
            if (nm->idFrom == IDC_VALUES)
                return ValueCustomDraw(d, (NMLVCUSTOMDRAW*)lParam);
        }
        if (nm->code == LVN_ITEMCHANGED && (((NMLISTVIEW*)lParam)->uChanged & LVIF_STATE)) {
            if (nm->idFrom == IDC_RESULTS) {
                int sel = ListView_GetNextItem(d->results, -1, LVNI_SELECTED);
                if (sel >= 0 && sel != d->selected)
                    SelectHit(d, sel);
            } else if (nm->idFrom == IDC_WHERE) {
                int sel = ListView_GetNextItem(d->where, -1, LVNI_SELECTED);
                if (sel >= 0 && sel != d->selectedWhere) {
                    d->selectedWhere = sel;
                    d->note.clear();
                    UpdateDetail(d);
                }
            }
        }
        if (nm->code == LVN_GETDISPINFOW) {
            LVITEMW& item = ((NMLVDISPINFOW*)lParam)->item;
            WString text = SpokenRow(d, nm->idFrom, item.iItem);
            if ((item.mask & LVIF_TEXT) && !text.empty())
                lstrcpynW(item.pszText, text.c_str(), item.cchTextMax);
        }
        if (nm->code == NM_SETFOCUS || nm->code == NM_KILLFOCUS)
            InvalidateRect(nm->hwndFrom, NULL, FALSE);  // the focus frame
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam), code = HIWORD(wParam);
        switch (id) {
        case IDC_CHASSIS:
            if (code == CBN_SELCHANGE) {
                d->chassisEdited = true;
                PostMessageW(hwnd, WM_APP_CHASSIS, 0, 0);  // the edit text updates after this
            } else if (code == CBN_KILLFOCUS) {
                SendMessageW(d->chassis, CB_SETEDITSEL, 0, MAKELPARAM(-1, 0));  // no stale highlight
                UpdateForChassis(d, d->chassisEdited);
            } else if (code == CBN_EDITCHANGE) {
                d->chassisEdited = true;
                if (!FolderForChassis(d->chassisList, GetWindowTextString(d->chassis)).empty())
                    UpdateForChassis(d, true);
            }
            return 0;
        case IDC_SEARCH:
            if (code == EN_CHANGE) {
                d->note.clear();
                RunSearch(d);
            } else if (code == EN_SETFOCUS || code == EN_KILLFOCUS) {
                InvalidateRect(hwnd, NULL, FALSE);  // the field's frame shows focus
            }
            return 0;
        case IDC_COPY_KEYWORD:
            Copy(d, false);
            return 0;
        case IDC_COPY_MODULE:
            Copy(d, true);
            return 0;
        case IDC_TRANSLATIONS:
            if (RunTranslationsDialog(hwnd)) {
                SetWindowTextW(d->translations, TranslationsLinkText().c_str());
                RunSearch(d);  // translations are searched too
                LayoutDialog(d);
                InvalidateRect(d->results, NULL, TRUE);
            }
            return 0;
        case IDC_REINDEX:
            if (d->indexing)
                FinishIndexing(d, true);
            else
                StartIndexing(d);
            return 0;
        case IDC_PROBLEMS:
            ShowProblems(d);
            return 0;
        case IDOK: {  // Enter: press the focused button; in the search field, go to the results
            HWND focus = GetFocus();
            if (focus == d->search && !d->hits.empty())
                SetFocus(d->results);
            else if (focus && GetParent(focus) == hwnd && IsWindowEnabled(focus) && focus != d->results &&
                     focus != d->where && focus != d->values && focus != d->search)
                SendMessageW(focus, BM_CLICK, 0, 0);
            return 0;
        }
        case IDCANCEL:
            d->done = true;
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        d->done = true;
        return 0;
    case WM_DESTROY:
        StopIndexing(d);  // cancelled: nothing is kept
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void RunExplorerDialog(HWND owner, const WString& workDir)
{
    ExplorerDialog* d = new ExplorerDialog();  // the index can be large: off the stack
    d->selected = -1;
    d->selectedWhere = -1;
    WString work = NormalizeDir(workDir);
    size_t slash = work.find_last_of(L'\\');
    d->expectedDaten = slash == WString::npos ? WString(L"DATEN") : JoinPath(work.substr(0, slash), L"DATEN");
    d->datenDir = FindDatenDir(work);
    d->chassisList = LoadChassisList(d->datenDir);
    HWND hwnd = CreateModal(owner, kDialogClass, DialogProc, L"Parameter Explorer", S(1000), S(680), d, true);
    if (hwnd)
        RunModal(owner, hwnd, d->search, &d->done);
    ImageList_Destroy(d->resultHeight);
    ImageList_Destroy(d->whereHeight);
    ImageList_Destroy(d->valueHeight);
    delete d;
}
