// Session history window. See history.h.

#include <windows.h>
#include <commctrl.h>

#include <vector>

#include "history.h"
#include "modal.h"
#include "session.h"
#include "sessiondlg.h"
#include "theme.h"
#include "util.h"

static const wchar_t kDialogClass[] = L"NcsWorkbenchHistory";

enum { IDC_SESSIONS = 100, IDC_ROWS, IDC_SHOW_FOLDER, IDC_EXPORT, IDC_OPEN = 110 };

static const int kFileSlots = 6;

struct HistoryFile {
    const wchar_t* kind;
    int snapId;
};

struct HistoryRow {
    WString name, original, intended, readback, result;
    COLORREF color;
};

struct HistoryDialog {
    HWND hwnd;
    HWND sessionList, rowList, showFolder, exportButton, close;
    HWND open[kFileSlots];
    HIMAGELIST sessionHeight, rowHeight;
    WString root;
    std::vector<Session> sessions;
    int selected;
    HistoryFile files[kFileSlots];
    int fileCount;
    std::vector<HistoryRow> rows;
    WString rowsNote;  // shown instead of an empty table
    bool done;
};

static HistoryDialog* g_painting;  // PaintBuffered takes a plain function

// ---------------------------------------------------------------------------
// Layout

struct Rects {
    RECT panel, sessions, detail, files, tableHeader, rows, close, exportButton, showFolder;
};

static Rects ComputeRects(int w, int h)
{
    Rects r;
    r.panel = { 0, 0, S(290), h };
    r.sessions = { 1, S(48), S(290), h - S(84) };
    int left = S(290) + S(32), right = w - S(32);
    r.detail = { left, 0, right, h };
    r.files = { left, S(128), right, S(128) + kFileSlots * S(28) };
    r.tableHeader = { left, r.files.bottom + S(40), right, r.files.bottom + S(66) };
    int by = h - S(24) - S(44);
    r.rows = { left, r.tableHeader.bottom, right, by - S(16) };
    r.close = { right - S(130), by, right, by + S(44) };
    r.exportButton = { r.close.left - S(8) - S(130), by, r.close.left - S(8), by + S(44) };
    r.showFolder = { r.exportButton.left - S(8) - S(170), by, r.exportButton.left - S(8), by + S(44) };
    return r;
}

static RECT FileRowRect(const Rects& r, int i)
{
    RECT row = { r.files.left, r.files.top + i * S(28), r.files.right, r.files.top + (i + 1) * S(28) };
    return row;
}

// Function | Original | To code | Read back | Result.
static void ColumnEdges(const RECT& table, int edges[5])
{
    int width = table.right - table.left;
    static const int pct[5] = { 0, 32, 50, 68, 84 };
    for (int i = 0; i < 5; ++i)
        edges[i] = table.left + width * pct[i] / 100;
}

static void LayoutDialog(HistoryDialog* d)
{
    RECT client;
    GetClientRect(d->hwnd, &client);
    Rects r = ComputeRects(client.right, client.bottom);
    Place(d->sessionList, r.sessions);
    Place(d->rowList, r.rows);
    Place(d->close, r.close);
    Place(d->exportButton, r.exportButton);
    Place(d->showFolder, r.showFolder);
    for (int i = 0; i < kFileSlots; ++i) {
        RECT row = FileRowRect(r, i);
        RECT link = { row.right - S(56), row.top + S(2), row.right, row.bottom - S(2) };
        Place(d->open[i], link);
    }
    FitListColumn(d->sessionList);
    FitListColumn(d->rowList);
    InvalidateRect(d->hwnd, NULL, FALSE);
}

// ---------------------------------------------------------------------------
// Selection

static void AddFile(HistoryDialog* d, const wchar_t* kind, const Snapshot* snap)
{
    if (snap && d->fileCount < kFileSlots) {
        d->files[d->fileCount].kind = kind;
        d->files[d->fileCount].snapId = snap->id;
        ++d->fileCount;
    }
}

static void Select(HistoryDialog* d, int index)
{
    d->selected = index;
    d->fileCount = 0;
    d->rows.clear();
    d->rowsNote.clear();
    if (index >= 0 && index < (int)d->sessions.size()) {
        const Session& s = d->sessions[index];
        AddFile(d, L"BEFORE TRC", LastSnapshot(s, SNAP_BEFORE_TRC));
        AddFile(d, L"BEFORE MAN", LastSnapshot(s, SNAP_BEFORE_MAN));
        AddFile(d, L"ORIGINAL", LastSnapshot(s, SNAP_READ));
        AddFile(d, L"FOR NCS DUMMY", LastSnapshot(s, SNAP_DUMMY));
        int man = s.coded ? s.coded : s.reviewed;
        AddFile(d, s.coded ? L"CODED MAN" : L"MAN EXPORT", man ? FindSnapshot(s, man) : LastSnapshot(s, SNAP_MAN));
        AddFile(d, L"READ-BACK", LastSnapshot(s, SNAP_READBACK));

        SessionVerification v;
        SessionReview review;
        if (LoadVerification(s, &v)) {
            for (size_t i = 0; i < v.rows.size(); ++i) {
                const VerifyRow& row = v.rows[i];
                HistoryRow h;
                h.name = Widen(row.name);
                h.original = row.original.empty() ? WString(L"-") : Widen(JoinValues(row.original));
                h.intended = Widen(JoinValues(row.intended));
                h.readback = row.readback.empty() ? WString(L"-") : Widen(JoinValues(row.readback));
                h.result = row.status == VERIFY_MATCH      ? L"MATCH"
                           : row.status == VERIFY_MISMATCH ? L"MISMATCH"
                                                           : L"CAN'T VERIFY";
                h.color = row.status == VERIFY_MATCH ? kGood : row.status == VERIFY_MISMATCH ? kBad : kWarn;
                d->rows.push_back(h);
            }
            if (d->rows.empty())
                d->rowsNote = L"The coded MAN changed nothing.";
        } else if (LoadReview(s, man, &review)) {
            for (size_t i = 0; i < review.rows.size(); ++i) {
                const DiffRow& row = review.rows[i];
                if (!IsIntendedChange(row))
                    continue;
                HistoryRow h;
                h.name = Widen(row.name);
                h.original = row.trc.empty() ? WString(L"-") : Widen(JoinValues(row.trc));
                h.intended = Widen(JoinValues(row.man));
                h.readback = L"-";
                h.result = L"NOT READ BACK";
                h.color = kFaint;
                d->rows.push_back(h);
            }
            if (d->rows.empty())
                d->rowsNote = L"The MAN matches the original trace.";
        } else {
            d->rowsNote = L"No MAN export in this session.";
        }
    }
    for (int i = 0; i < kFileSlots; ++i)
        ShowWindow(d->open[i], i < d->fileCount ? SW_SHOW : SW_HIDE);
    bool any = index >= 0;
    EnableWindow(d->exportButton, any);
    EnableWindow(d->showFolder, any);
    ListView_SetItemCountEx(d->rowList, (int)d->rows.size(), 0);
    ShowWindow(d->rowList, d->rows.empty() ? SW_HIDE : SW_SHOW);
    FitListColumn(d->rowList);
    InvalidateRect(d->rowList, NULL, TRUE);
    InvalidateRect(d->hwnd, NULL, FALSE);
}

// ---------------------------------------------------------------------------
// Painting

static void PaintDialog(HDC dc, const RECT& client)
{
    HistoryDialog* d = g_painting;
    Rects r = ComputeRects(client.right, client.bottom);
    Fill(dc, client, kPage);

    Fill(dc, r.panel, kPanel);
    RECT divider = { r.panel.right, 0, r.panel.right + 1, client.bottom };
    Fill(dc, divider, kRule);
    wchar_t head[32];
    wsprintfW(head, L"SESSIONS / %02d", (int)d->sessions.size());
    RECT t = { S(16), S(22), r.panel.right - S(16), S(38) };
    DrawLabel(dc, g_fonts.label, kFaint, head, t, DT_LEFT | DT_VCENTER, Tracking());
    t = { S(16), client.bottom - S(64), r.panel.right - S(16), client.bottom - S(48) };
    DrawLabel(dc, g_fonts.label, kFaint, L"SESSIONS FOLDER", t, DT_LEFT | DT_VCENTER, Tracking());
    t = { S(16), client.bottom - S(44), r.panel.right - S(16), client.bottom - S(22) };
    DrawLabel(dc, g_fonts.body, kSecondary, d->root.c_str(), t, DT_LEFT | DT_VCENTER | DT_PATH_ELLIPSIS);
    if (d->sessions.empty()) {
        t = { S(16), S(60), r.panel.right - S(16), S(80) };
        DrawLabel(dc, g_fonts.body, kSecondary, L"No sessions yet.", t, DT_LEFT | DT_VCENTER);
    }

    if (d->selected < 0)
        return;
    const Session& s = d->sessions[d->selected];
    int left = r.detail.left, right = r.detail.right;
    t = { left, S(22), right, S(38) };
    DrawLabel(dc, g_fonts.label, kFaint, (L"SESSION / " + s.id).c_str(), t, DT_LEFT | DT_VCENTER, Tracking());
    WString result = SessionResultText(s);
    int resultWidth = MeasureLabel(dc, g_fonts.label, result.c_str(), Tracking()) + S(4);
    t = { right - resultWidth, S(44), right, S(74) };
    DrawLabel(dc, g_fonts.label, SessionResultColor(s), result.c_str(), t, DT_RIGHT | DT_VCENTER, Tracking());
    t = { left, S(44), right - resultWidth - S(16), S(74) };
    DrawLabel(dc, g_fonts.heading, kWhite, SessionEcuText(s).c_str(), t, DT_LEFT | DT_VCENTER);

    WString line;
    if (!s.vehicle.empty())
        line = s.vehicle + L" / ";
    line += FormatDay(s.started);
    if (HasTime(s.finished))
        line += L" TO " + FormatDay(s.finished);
    if (HasTime(s.codedAt))
        line += L" / CODING REPORTED BY YOU " + FormatClock(s.codedAt);
    CharUpperBuffW(&line[0], (DWORD)line.size());
    t = { left, S(80), right, S(96) };
    DrawLabel(dc, g_fonts.label, kMuted, line.c_str(), t, DT_LEFT | DT_VCENTER, Tracking());

    t = { left, S(108), right, S(124) };
    DrawLabel(dc, g_fonts.label, kFaint, L"FILES", t, DT_LEFT | DT_VCENTER, Tracking());
    if (d->fileCount == 0) {
        t = { left, r.files.top, right, r.files.top + S(28) };
        DrawLabel(dc, g_fonts.body, kSecondary, L"No files were saved.", t, DT_LEFT | DT_VCENTER);
    }
    for (int i = 0; i < d->fileCount; ++i) {
        const Snapshot* snap = FindSnapshot(s, d->files[i].snapId);
        RECT row = FileRowRect(r, i);
        RECT c = { row.left, row.top, row.left + S(116), row.bottom };
        DrawLabel(dc, g_fonts.label, kFaint, d->files[i].kind, c, DT_LEFT | DT_VCENTER, Tracking());
        c = { row.left + S(124), row.top, row.right - S(190), row.bottom };
        DrawLabel(dc, g_fonts.mono, kWhite, snap->file.c_str(), c, DT_LEFT | DT_VCENTER);
        WString meta = FormatThousands(snap->size) + L" B / " + FormatClock(snap->taken);
        c = { row.right - S(186), row.top, row.right - S(68), row.bottom };
        DrawLabel(dc, g_fonts.label, kFaint, meta.c_str(), c, DT_RIGHT | DT_VCENTER, Tracking());
        RECT rule = { row.left, row.bottom - 1, row.right, row.bottom };
        Fill(dc, rule, kTile2);
    }

    t = { left, r.tableHeader.top - S(22), right, r.tableHeader.top - S(6) };
    DrawLabel(dc, g_fonts.label, kFaint, L"CHANGES AND READ-BACK", t, DT_LEFT | DT_VCENTER, Tracking());
    Fill(dc, r.tableHeader, kTile1);
    RECT table = r.tableHeader;
    if (IsWindowVisible(d->rowList)) {
        RECT lc;
        GetClientRect(d->rowList, &lc);
        table.right = table.left + lc.right;
    }
    int edges[5];
    ColumnEdges(table, edges);
    const wchar_t* heads[5] = { L"FUNCTION", L"ORIGINAL", L"TO CODE", L"READ BACK", L"RESULT" };
    for (int i = 0; i < 5; ++i) {
        int cl = i == 0 ? edges[0] + S(14) : edges[i];
        int cr = i == 4 ? table.right - S(12) : edges[i + 1] - S(10);
        RECT c = { cl, r.tableHeader.top, cr, r.tableHeader.bottom };
        DrawLabel(dc, g_fonts.label, kFaint, heads[i], c, (i == 4 ? DT_RIGHT : DT_LEFT) | DT_VCENTER, Tracking());
    }
    if (d->rows.empty()) {
        Fill(dc, r.rows, kTile3);
        RECT m = { r.rows.left + S(14), r.rows.top + S(16), r.rows.right - S(14), r.rows.top + S(40) };
        DrawLabel(dc, g_fonts.body, kSecondary, d->rowsNote.c_str(), m, DT_LEFT | DT_VCENTER);
    }
}

static LRESULT SessionCustomDraw(HistoryDialog* d, NMLVCUSTOMDRAW* cd)
{
    switch (cd->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT: {
        int item = (int)cd->nmcd.dwItemSpec;
        if (item < 0 || item >= (int)d->sessions.size())
            break;
        const Session& s = d->sessions[item];
        HDC dc = cd->nmcd.hdc;
        UINT state = ListView_GetItemState(d->sessionList, item, LVIS_SELECTED | LVIS_FOCUSED);
        RECT rc;
        ListView_GetItemRect(d->sessionList, item, &rc, LVIR_BOUNDS);
        Fill(dc, rc, (state & LVIS_SELECTED) ? kSelect : kPanel);
        RECT c = { rc.left + S(16), rc.top + S(8), rc.right - S(12), rc.top + S(24) };
        DrawLabel(dc, g_fonts.label, kMuted, FormatDay(s.started).c_str(), c, DT_LEFT | DT_VCENTER, Tracking());
        DrawLabel(dc, g_fonts.label, SessionResultColor(s), SessionResultText(s).c_str(), c, DT_RIGHT | DT_VCENTER,
                  Tracking());
        c = { rc.left + S(16), rc.top + S(26), rc.right - S(12), rc.bottom - S(6) };
        DrawLabel(dc, g_fonts.mono, kWhite, SessionEcuText(s).c_str(), c, DT_LEFT | DT_VCENTER);
        if ((state & LVIS_FOCUSED) && GetFocus() == d->sessionList)
            Frame(dc, rc, kAccent);
        return CDRF_SKIPDEFAULT;
    }
    }
    return CDRF_DODEFAULT;
}

static LRESULT RowCustomDraw(HistoryDialog* d, NMLVCUSTOMDRAW* cd)
{
    switch (cd->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT: {
        int item = (int)cd->nmcd.dwItemSpec;
        if (item < 0 || item >= (int)d->rows.size())
            break;
        const HistoryRow& row = d->rows[item];
        HDC dc = cd->nmcd.hdc;
        UINT state = ListView_GetItemState(d->rowList, item, LVIS_SELECTED | LVIS_FOCUSED);
        RECT rc;
        ListView_GetItemRect(d->rowList, item, &rc, LVIR_BOUNDS);
        Fill(dc, rc, (state & LVIS_SELECTED) ? kSelect : kTile3);
        RECT rule = { rc.left, rc.bottom - 1, rc.right, rc.bottom };
        Fill(dc, rule, kTile1);
        int edges[5];
        ColumnEdges(rc, edges);
        const WString* cells[4] = { &row.name, &row.original, &row.intended, &row.readback };
        COLORREF colors[4] = { kWhite, kSecondary, kAccent, row.color == kFaint ? kFaint : row.color };
        for (int i = 0; i < 4; ++i) {
            RECT c = { i == 0 ? edges[0] + S(14) : edges[i], rc.top, edges[i + 1] - S(10), rc.bottom };
            DrawLabel(dc, g_fonts.mono, colors[i], cells[i]->c_str(), c, DT_LEFT | DT_VCENTER);
        }
        RECT c = { edges[4], rc.top, rc.right - S(12), rc.bottom };
        DrawLabel(dc, g_fonts.label, row.color, row.result.c_str(), c, DT_RIGHT | DT_VCENTER, Tracking());
        if ((state & LVIS_FOCUSED) && GetFocus() == d->rowList)
            Frame(dc, rc, kAccent);
        return CDRF_SKIPDEFAULT;
    }
    }
    return CDRF_DODEFAULT;
}

// ---------------------------------------------------------------------------
// Window

static HWND CreateList(HistoryDialog* d, int id, COLORREF bg, int rowHeight, HIMAGELIST* images)
{
    HWND list = CreateWindowExW(0, WC_LISTVIEWW, L"",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_NOCOLUMNHEADER | LVS_SINGLESEL |
                                    LVS_SHOWSELALWAYS | LVS_OWNERDATA,
                                0, 0, 0, 0, d->hwnd, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    ListView_SetBkColor(list, bg);
    SendMessageW(list, WM_SETFONT, (WPARAM)g_fonts.body, FALSE);
    *images = ImageList_Create(1, S(rowHeight), ILC_COLOR32, 1, 0);  // sets report row height (XP-safe)
    ListView_SetImageList(list, *images, LVSIL_SMALL);
    LVCOLUMNW col = {};
    col.mask = LVCF_WIDTH;
    col.cx = S(400);
    ListView_InsertColumn(list, 0, &col);
    ApplyThemedControl(list, L"DarkMode_Explorer");
    return list;
}

static void CreateDialogControls(HistoryDialog* d)
{
    d->sessionList = CreateList(d, IDC_SESSIONS, kPanel, 52, &d->sessionHeight);
    d->rowList = CreateList(d, IDC_ROWS, kTile3, 30, &d->rowHeight);
    for (int i = 0; i < kFileSlots; ++i)
        d->open[i] = CreateThemedButton(d->hwnd, IDC_OPEN + i, L"OPEN", BUTTON_LINK, kPage);
    d->showFolder = CreateThemedButton(d->hwnd, IDC_SHOW_FOLDER, L"SHOW FOLDER", BUTTON_SECONDARY, kPage);
    d->exportButton = CreateThemedButton(d->hwnd, IDC_EXPORT, L"EXPORT", BUTTON_SECONDARY, kPage);
    d->close = CreateThemedButton(d->hwnd, IDCANCEL, L"CLOSE", BUTTON_PRIMARY, kPage);
    ListView_SetItemCountEx(d->sessionList, (int)d->sessions.size(), 0);
    LayoutDialog(d);
    if (!d->sessions.empty())
        ListView_SetItemState(d->sessionList, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    Select(d, d->sessions.empty() ? -1 : 0);
}

static void ExportSelected(HistoryDialog* d)
{
    if (d->selected < 0)
        return;
    const Session& s = d->sessions[d->selected];
    WString parent = PickFolder(d->hwnd, L"Choose where to copy the session's files.", WString());
    if (parent.empty())
        return;
    WString created;
    WString error = ExportSession(s, parent, &created);
    if (error.empty())
        RunMessageDialog(d->hwnd, L"Export", L"SESSION / " + s.id, L"Session exported",
                         L"All of the session's files were copied to " + created + L".");
    else
        RunMessageDialog(d->hwnd, L"Export", L"SESSION / " + s.id, L"Export stopped", error);
}

static LRESULT CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    HistoryDialog* d = (HistoryDialog*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_NCCREATE:
        d = (HistoryDialog*)((CREATESTRUCTW*)lParam)->lpCreateParams;
        d->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)d);
        break;
    case WM_CREATE:
        CreateDialogControls(d);
        return 0;
    case WM_SIZE:
        if (d && d->rowList)
            LayoutDialog(d);
        return 0;
    case WM_GETMINMAXINFO: {
        SIZE min = ModalWindowSize(S(780), S(540), true);
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
        if (nm->code == NM_CUSTOMDRAW && nm->idFrom == IDC_SESSIONS)
            return SessionCustomDraw(d, (NMLVCUSTOMDRAW*)lParam);
        if (nm->code == NM_CUSTOMDRAW && nm->idFrom == IDC_ROWS)
            return RowCustomDraw(d, (NMLVCUSTOMDRAW*)lParam);
        if (nm->idFrom == IDC_SESSIONS && nm->code == LVN_ITEMCHANGED) {
            NMLISTVIEW* lv = (NMLISTVIEW*)lParam;
            int sel = ListView_GetNextItem(d->sessionList, -1, LVNI_SELECTED);
            if ((lv->uChanged & LVIF_STATE) && sel >= 0 && sel != d->selected)
                Select(d, sel);
        }
        if (nm->code == LVN_GETDISPINFOW) {
            // Rows are painted by hand; this text is what screen readers announce.
            LVITEMW& item = ((NMLVDISPINFOW*)lParam)->item;
            WString text;
            if (nm->idFrom == IDC_SESSIONS && item.iItem >= 0 && item.iItem < (int)d->sessions.size()) {
                const Session& s = d->sessions[item.iItem];
                text = FormatDay(s.started) + L", " + SessionEcuText(s) + L", " + SessionResultText(s);
            } else if (nm->idFrom == IDC_ROWS && item.iItem >= 0 && item.iItem < (int)d->rows.size()) {
                const HistoryRow& row = d->rows[item.iItem];
                text = row.name + L": " + row.original + L" to " + row.intended + L", read back " + row.readback +
                       L", " + row.result;
            }
            if ((item.mask & LVIF_TEXT) && !text.empty())
                lstrcpynW(item.pszText, text.c_str(), item.cchTextMax);
        }
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id >= IDC_OPEN && id < IDC_OPEN + kFileSlots && d->selected >= 0) {
            const Session& s = d->sessions[d->selected];
            const Snapshot* snap = FindSnapshot(s, d->files[id - IDC_OPEN].snapId);
            if (snap)
                OpenInNotepad(SnapshotPath(s, *snap));
            return 0;
        }
        switch (id) {
        case IDC_SHOW_FOLDER:
            if (d->selected >= 0)
                ShowInExplorer(JoinPath(d->sessions[d->selected].dir, L"session.ini"));
            return 0;
        case IDC_EXPORT:
            ExportSelected(d);
            return 0;
        case IDOK: {  // Enter presses the focused button
            HWND focus = GetFocus();
            if (focus != d->sessionList && focus != d->rowList && IsWindowEnabled(focus))
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
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void RunHistoryDialog(HWND owner, const WString& sessionsRoot)
{
    HistoryDialog d = {};
    d.root = sessionsRoot;
    d.sessions = ListSessions(sessionsRoot);
    d.selected = -1;
    HWND hwnd = CreateModal(owner, kDialogClass, DialogProc, L"Session history", S(960), S(640), &d, true);
    if (!hwnd)
        return;
    RunModal(owner, hwnd, d.sessions.empty() ? d.close : d.sessionList, &d.done);
    ImageList_Destroy(d.sessionHeight);
    ImageList_Destroy(d.rowHeight);
}
