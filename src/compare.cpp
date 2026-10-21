// Compare window. See compare.h.

#include <windows.h>
#include <commctrl.h>

#include <vector>

#include "compare.h"
#include "modal.h"
#include "theme.h"
#include "transdlg.h"
#include "util.h"

static const wchar_t kDialogClass[] = L"NcsWorkbenchCompare";

enum { IDC_ROWS = 100, IDC_TOGGLE };

struct CompareDialog {
    HWND hwnd;
    HWND list, toggle, close;
    HIMAGELIST rowHeight;
    std::vector<DiffRow> rows;
    std::vector<size_t> visible;  // indexes into rows for the current filter
    DiffSummary summary;
    bool loaded;
    bool showAll;
    bool done;
};

static CompareDialog* g_painting;  // PaintBuffered takes a plain function

static bool LoadTrace(const WString& path, std::vector<TraceEntry>* entries)
{
    String data;
    if (!ReadFileBytes(path, &data))
        return false;
    *entries = ParseTrace(data.data(), data.size());
    return true;
}

bool SummarizeDiff(const WString& trcPath, const WString& manPath, DiffSummary* summary)
{
    std::vector<TraceEntry> trc, man;
    if (!LoadTrace(trcPath, &trc) || !LoadTrace(manPath, &man))
        return false;
    DiffTrace(trc, man, summary);
    return true;
}


// ---------------------------------------------------------------------------
// Layout

struct Rects {
    RECT toggle, header, list, close, message;
};

static Rects ComputeRects(int w, int h)
{
    int pad = S(24);
    Rects r;
    r.toggle = { w - pad - S(150), S(36), w - pad, S(72) };
    r.close = { w - pad - S(120), h - pad - S(44), w - pad, h - pad };
    r.header = { pad, S(112), w - pad, S(140) };
    r.list = { pad, S(140), w - pad, r.close.top - S(16) };
    r.message = r.list;
    return r;
}

// Function | TRC | MAN, 46/27/27 of the table width.
static void ColumnEdges(const RECT& table, int* trcLeft, int* manLeft)
{
    int width = table.right - table.left;
    *trcLeft = table.left + width * 46 / 100;
    *manLeft = table.left + width * 73 / 100;
}

static void ApplyFilter(CompareDialog* d)
{
    d->visible.clear();
    for (size_t i = 0; i < d->rows.size(); ++i) {
        if (d->showAll || IsNotable(d->rows[i], d->summary))
            d->visible.push_back(i);
    }
    ListView_SetItemCountEx(d->list, (int)d->visible.size(), 0);
    ShowWindow(d->list, d->visible.empty() ? SW_HIDE : SW_SHOW);
    FitListColumn(d->list);
    SetWindowTextW(d->toggle, d->showAll ? L"CHANGES ONLY" : L"SHOW ALL");
    InvalidateRect(d->list, NULL, TRUE);
    InvalidateRect(d->hwnd, NULL, FALSE);
}

static void LayoutDialog(CompareDialog* d)
{
    RECT client;
    GetClientRect(d->hwnd, &client);
    Rects r = ComputeRects(client.right, client.bottom);
    Place(d->toggle, r.toggle);
    Place(d->close, r.close);
    Place(d->list, r.list);
    FitListColumn(d->list);
    InvalidateRect(d->hwnd, NULL, FALSE);
}

// ---------------------------------------------------------------------------
// Painting

static void PaintDialog(HDC dc, const RECT& client)
{
    CompareDialog* d = g_painting;
    int pad = S(24), w = client.right;
    Rects r = ComputeRects(client.right, client.bottom);
    Fill(dc, client, kPage);

    RECT t = { pad, S(24), r.toggle.left - S(16), S(40) };
    DrawLabel(dc, g_fonts.label, kFaint, L"03 / COMPARE", t, DT_LEFT | DT_VCENTER, Tracking());
    t = { pad, S(44), r.toggle.left - S(16), S(74) };
    DrawLabel(dc, g_fonts.heading, kWhite, L"FSW_PSW.TRC vs FSW_PSW.MAN", t, DT_LEFT | DT_VCENTER);

    t = { pad, S(84), w - pad, S(100) };
    if (!d->loaded) {
        DrawLabel(dc, g_fonts.label, kWarn, L"COULDN'T READ FSW_PSW.TRC OR FSW_PSW.MAN", t, DT_LEFT | DT_VCENTER,
                  Tracking());
    } else {
        const DiffSummary& s = d->summary;
        wchar_t text[160];
        wsprintfW(text, L"%d CHANGED / %d ONLY IN TRC / %d ONLY IN MAN / %d SAME%s", s.changed, s.onlyTrc,
                  s.onlyMan, s.same, s.partialMan ? L" / PARTIAL MAN" : L"");
        DrawLabel(dc, g_fonts.label, kMuted, text, t, DT_LEFT | DT_VCENTER, Tracking());
    }

    Fill(dc, r.header, kTile1);
    // Columns follow the rows, which end where the list's scrollbar starts.
    RECT table = r.header;
    if (IsWindowVisible(d->list)) {
        RECT lc;
        GetClientRect(d->list, &lc);
        table.right = table.left + lc.right;
    }
    int trcLeft, manLeft;
    ColumnEdges(table, &trcLeft, &manLeft);
    RECT c = { r.header.left + S(16), r.header.top, trcLeft - S(12), r.header.bottom };
    DrawLabel(dc, g_fonts.label, kFaint, L"FUNCTION", c, DT_LEFT | DT_VCENTER, Tracking());
    c = { trcLeft, r.header.top, manLeft - S(12), r.header.bottom };
    DrawLabel(dc, g_fonts.label, kFaint, L"TRC (READ FROM CAR)", c, DT_LEFT | DT_VCENTER, Tracking());
    c = { manLeft, r.header.top, r.header.right - S(12), r.header.bottom };
    DrawLabel(dc, g_fonts.label, kFaint, L"MAN (TO CODE)", c, DT_LEFT | DT_VCENTER, Tracking());

    if (d->visible.empty()) {
        Fill(dc, r.message, kTile3);
        RECT m = r.message;
        m.top += S(32);
        m.left += S(16);
        m.right -= S(16);
        RECT line = { m.left, m.top, m.right, m.top + S(24) };
        const wchar_t* headline = !d->loaded           ? L"Nothing to compare."
                                  : d->rows.empty()     ? L"Both files are empty."
                                                        : L"No changes. The MAN matches the TRC.";
        DrawLabel(dc, g_fonts.title, d->loaded && !d->rows.empty() ? kGood : kWhite, headline, line,
                  DT_CENTER | DT_VCENTER);
        line = { m.left, m.top + S(32), m.right, m.top + S(48) };
        if (d->loaded && !d->rows.empty())
            DrawLabel(dc, g_fonts.label, kMuted, L"SHOW ALL LISTS EVERY FUNCTION", line, DT_CENTER | DT_VCENTER,
                      Tracking());
    }
}

static LRESULT RowCustomDraw(CompareDialog* d, NMLVCUSTOMDRAW* cd)
{
    switch (cd->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT: {
        int item = (int)cd->nmcd.dwItemSpec;
        if (item < 0 || (size_t)item >= d->visible.size())
            break;
        const DiffRow& row = d->rows[d->visible[item]];
        HDC dc = cd->nmcd.hdc;
        UINT state = ListView_GetItemState(d->list, item, LVIS_SELECTED | LVIS_FOCUSED);
        bool selected = (state & LVIS_SELECTED) != 0;
        RECT rc;
        ListView_GetItemRect(d->list, item, &rc, LVIR_BOUNDS);
        Fill(dc, rc, selected ? kSelect : kTile3);

        // Accent bar: blue = changed, amber = only in one file.
        bool flagOnlyTrc = row.status == DIFF_ONLY_TRC && !d->summary.partialMan;
        if (row.status == DIFF_CHANGED || row.status == DIFF_ONLY_MAN || flagOnlyTrc) {
            RECT bar = { rc.left, rc.top, rc.left + S(3), rc.bottom };
            Fill(dc, bar, row.status == DIFF_CHANGED ? kAccent : kWarn);
        }
        RECT rule = { rc.left, rc.bottom - 1, rc.right, rc.bottom };
        Fill(dc, rule, kTile1);

        int trcLeft, manLeft;
        ColumnEdges(rc, &trcLeft, &manLeft);
        bool same = row.status == DIFF_SAME;
        RECT c = { rc.left + S(16), rc.top, trcLeft - S(12), rc.bottom };
        // Mono: in Onest the zero of names like C0A reads as an O.
        DrawKeyword(dc, row.name, c, same ? kSecondary : kWhite);

        c = { trcLeft, rc.top, manLeft - S(12), rc.bottom };
        if (row.status == DIFF_ONLY_MAN)
            DrawLabel(dc, g_fonts.label, kWarn, L"NOT IN TRC", c, DT_LEFT | DT_VCENTER, Tracking());
        else
            DrawValues(dc, row.trc, c, same ? kMuted : kText);

        c = { manLeft, rc.top, rc.right - S(12), rc.bottom };
        if (row.status == DIFF_ONLY_TRC)
            DrawLabel(dc, g_fonts.label, flagOnlyTrc ? kWarn : kFaint, L"NOT IN MAN", c, DT_LEFT | DT_VCENTER,
                      Tracking());
        else
            DrawValues(dc, row.man, c, row.status == DIFF_CHANGED ? kAccent : same ? kMuted : kText);

        if ((state & LVIS_FOCUSED) && GetFocus() == d->list)
            Frame(dc, rc, kAccent);
        return CDRF_SKIPDEFAULT;
    }
    }
    return CDRF_DODEFAULT;
}

// ---------------------------------------------------------------------------
// Window

static void CreateDialogControls(CompareDialog* d)
{
    d->toggle = CreateThemedButton(d->hwnd, IDC_TOGGLE, L"SHOW ALL", BUTTON_SECONDARY, kPage);
    d->close = CreateThemedButton(d->hwnd, IDCANCEL, L"CLOSE", BUTTON_SECONDARY, kPage);

    // Virtual list: rows come from d->visible, drawn in RowCustomDraw.
    // Two-line rows while translations show.
    d->list = CreateThemedList(d->hwnd, IDC_ROWS, kTile3, KeywordRowHeight(32), &d->rowHeight);

    LayoutDialog(d);
    ApplyFilter(d);
}

static LRESULT CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    CompareDialog* d = (CompareDialog*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_NCCREATE:
        d = (CompareDialog*)((CREATESTRUCTW*)lParam)->lpCreateParams;
        d->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)d);
        break;

    case WM_CREATE:
        CreateDialogControls(d);
        return 0;

    case WM_SIZE:
        if (d && d->list)
            LayoutDialog(d);
        return 0;

    case WM_GETMINMAXINFO: {
        SIZE min = ModalWindowSize(S(560), S(400), true);
        ((MINMAXINFO*)lParam)->ptMinTrackSize.x = min.cx;
        ((MINMAXINFO*)lParam)->ptMinTrackSize.y = min.cy;
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        g_painting = d;
        PaintBuffered(hwnd, PaintDialog);
        return 0;

    case WM_DRAWITEM:
        DrawThemedButton((const DRAWITEMSTRUCT*)lParam);
        return TRUE;

    case WM_NOTIFY: {
        NMHDR* nm = (NMHDR*)lParam;
        if (nm->idFrom == IDC_ROWS && nm->code == NM_CUSTOMDRAW)
            return RowCustomDraw(d, (NMLVCUSTOMDRAW*)lParam);
        if (nm->idFrom == IDC_ROWS && nm->code == LVN_GETDISPINFOW) {
            // Rows are painted by hand; this text is what screen readers announce.
            LVITEMW& item = ((NMLVDISPINFOW*)lParam)->item;
            if ((item.mask & LVIF_TEXT) && item.iItem >= 0 && (size_t)item.iItem < d->visible.size()) {
                const DiffRow& row = d->rows[d->visible[item.iItem]];
                WString text = SpokenKeyword(row.name) + L": TRC " +
                               (row.status == DIFF_ONLY_MAN ? WString(L"not present") : SpokenValues(row.trc)) +
                               L", MAN " +
                               (row.status == DIFF_ONLY_TRC ? WString(L"not present") : SpokenValues(row.man));
                lstrcpynW(item.pszText, text.c_str(), item.cchTextMax);
            }
        }
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_TOGGLE:
            d->showAll = !d->showAll;
            ApplyFilter(d);
            return 0;
        case IDOK: {  // Enter presses the focused button, else closes
            HWND focus = GetFocus();
            if (focus == d->toggle)
                SendMessageW(focus, BM_CLICK, 0, 0);
            else if (focus != d->list)
                d->done = true;
            return 0;
        }
        case IDCANCEL:
            d->done = true;
            return 0;
        }
        break;

    case WM_CLOSE:
        d->done = true;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void RunCompareDialog(HWND owner, const WString& trcPath, const WString& manPath)
{
    CompareDialog d = {};
    std::vector<TraceEntry> trc, man;
    d.loaded = LoadTrace(trcPath, &trc) && LoadTrace(manPath, &man);
    if (d.loaded)
        d.rows = DiffTrace(trc, man, &d.summary);

    HWND hwnd = CreateModal(owner, kDialogClass, DialogProc, L"Compare TRC with MAN", S(820), S(580), &d, true);
    if (!hwnd)
        return;
    RunModal(owner, hwnd, d.visible.empty() ? d.toggle : d.list, &d.done);
    ImageList_Destroy(d.rowHeight);
}
