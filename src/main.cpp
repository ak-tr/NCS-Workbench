// NCS Workbench - main window.
// Layout C: numbered coding steps on the left, WORK folder files on the right.
// XP-era APIs only (_WIN32_WINNT=0x0501); see build.cmd for flags.

#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <vector>

#include "backup.h"
#include "compare.h"
#include "man.h"
#include "resource.h"
#include "theme.h"
#include "util.h"
#include "version.h"
#include "xpstd.h"

static const wchar_t kWindowClass[] = L"NcsWorkbenchMain";
static const wchar_t kAppTitle[] = L"NCS Workbench";
static const wchar_t kDefaultWorkDir[] = L"C:\\NCSEXPER\\WORK";
static const wchar_t kTrcName[] = L"FSW_PSW.TRC";
static const wchar_t kManName[] = L"FSW_PSW.MAN";

// Change notifications are the main trigger; the poll covers folders that
// don't exist yet and shares (e.g. VM shared folders) that never signal.
static const UINT kPollMs = 3000;
// NCS Expert writes several files in a burst; refresh once after it settles.
static const UINT kDebounceMs = 300;

enum {
    IDC_BACKUP = 100,
    IDC_EMPTY,
    IDC_COMPARE,
    IDC_SHOW_BACKUP,
    IDC_BACKUP_DIR,
    IDC_FILES,
    IDC_WORK_DIR,
};

enum { TIMER_REFRESH = 1, TIMER_POLL };

enum FolderState { FOLDER_OK, FOLDER_MISSING, FOLDER_UNREADABLE };

static const int kStepCount = 3;
static const wchar_t* const kStepTitles[kStepCount] = {
    L"Back up the TRC",
    L"Empty the MAN",
    L"Compare TRC with MAN",
};
static const COLORREF kStepColors[kStepCount] = { kTile1, kTile2, kTile3 };

struct FileEntry {
    WString name;
    ULONGLONG size;
    FILETIME modified;
};

struct Layout {
    RECT header, rows[kStepCount], notice, backupDir;
    RECT panel, panelHead, list, workDir, footer;
    bool compact;  // narrow window: no big step numbers, smaller step buttons
};

struct App {
    HWND hwnd = NULL;
    HWND stepButtons[kStepCount] = {};
    HWND showBackup = NULL, backupDirButton = NULL, list = NULL, workDirButton = NULL;
    HWND lastFocus = NULL;
    HIMAGELIST rowHeight = NULL;
    Layout lay = {};

    WString workDir, backupDir, lastBackup;
    bool backupJustNow = false;  // a backup was made in this session
    bool backupWasMan = false;   // ...by Empty MAN rather than Back up TRC
    SYSTEMTIME backupTime = {};

    std::vector<FileEntry> files;
    FolderState folderState = FOLDER_MISSING;
    // TRC vs MAN for step 03's status line, recomputed when the files change.
    bool canCompare = false;  // both present, MAN not empty
    bool diffRead = false;
    DiffSummary diff;
    HANDLE watch = INVALID_HANDLE_VALUE;
    bool refreshPending = false;
};

static App g;

// ---------------------------------------------------------------------------
// WORK folder scan and watch

static bool ScanFiles(const WString& dir, std::vector<FileEntry>& out)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(JoinPath(dir, L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_FILE_NOT_FOUND;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        FileEntry e;
        e.name = fd.cFileName;
        e.size = ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        e.modified = fd.ftLastWriteTime;
        out.push_back(e);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return true;
}

static bool FileLess(const FileEntry& a, const FileEntry& b)
{
    return lstrcmpiW(a.name.c_str(), b.name.c_str()) < 0;
}

static bool SameFiles(const std::vector<FileEntry>& a, const std::vector<FileEntry>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].name != b[i].name || a[i].size != b[i].size ||
            CompareFileTime(&a[i].modified, &b[i].modified) != 0)
            return false;
    }
    return true;
}

static const FileEntry* FindFile(const wchar_t* name)
{
    for (size_t i = 0; i < g.files.size(); ++i) {
        if (lstrcmpiW(g.files[i].name.c_str(), name) == 0)
            return &g.files[i];
    }
    return NULL;
}

static bool IsKeyFile(const WString& name)
{
    return lstrcmpiW(name.c_str(), kTrcName) == 0 || lstrcmpiW(name.c_str(), kManName) == 0 ||
           lstrcmpiW(name.c_str(), L"NETTODAT.TRC") == 0;
}

static void StopWatch()
{
    if (g.watch != INVALID_HANDLE_VALUE) {
        FindCloseChangeNotification(g.watch);
        g.watch = INVALID_HANDLE_VALUE;
    }
}

static void StartWatch()
{
    StopWatch();
    g.watch = FindFirstChangeNotificationW(
        g.workDir.c_str(), FALSE,
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE);
}

// ---------------------------------------------------------------------------
// Layout

static void ComputeLayout(int w, int h)
{
    Layout& l = g.lay;
    int pad = S(20);
    l.footer = { 0, h - S(34), w, h };
    // The files panel takes 30% of the width, between 200 and 280 px.
    int panelWidth = std::min(S(280), std::max(S(200), w * 3 / 10));
    l.panel = { w - panelWidth, 0, w, l.footer.top };

    int left = pad, right = l.panel.left - pad;
    l.compact = right - left < S(470);
    l.header = { left, pad, right, pad + S(16) };
    int y = S(48);
    for (int i = 0; i < kStepCount; ++i) {
        l.rows[i] = { left, y, right, y + S(100) };
        y += S(102);
    }
    l.notice = { left, y + S(14), right, y + S(14) + S(60) };
    l.backupDir = { left, l.footer.top - pad - S(44), right, l.footer.top - pad };

    l.panelHead = { l.panel.left + S(16), pad, w - S(16), pad + S(16) };
    l.workDir = { l.panel.left + S(16), l.footer.top - pad - S(44), w - S(16), l.footer.top - pad };
    l.list = { l.panel.left + 1, S(48), w, l.workDir.top - S(16) };
}

static RECT StepButtonRect(int i)
{
    const RECT& r = g.lay.rows[i];
    int cy = (r.top + r.bottom) / 2;
    int width = g.lay.compact ? S(104) : S(132);
    RECT b = { r.right - S(20) - width, cy - S(24), r.right - S(20), cy + S(24) };
    return b;
}

// "CHANGE"-style link in the top-right of a label + value block.
static RECT BlockLinkRect(const RECT& block)
{
    RECT b = { block.right - S(80), block.top + S(18), block.right, block.top + S(42) };
    return b;
}

static RECT NoticeLinkRect()
{
    const RECT& r = g.lay.notice;
    int cy = (r.top + r.bottom) / 2;
    RECT b = { r.right - S(16) - S(80), cy - S(12), r.right - S(16), cy + S(12) };
    return b;
}

static void LayoutChildren()
{
    if (!g.list)
        return;
    RECT rc;
    GetClientRect(g.hwnd, &rc);
    ComputeLayout(rc.right, rc.bottom);
    for (int i = 0; i < kStepCount; ++i)
        Place(g.stepButtons[i], StepButtonRect(i));
    Place(g.showBackup, NoticeLinkRect());
    Place(g.backupDirButton, BlockLinkRect(g.lay.backupDir));
    Place(g.workDirButton, BlockLinkRect(g.lay.workDir));
    Place(g.list, g.lay.list);
    FitListColumn(g.list);  // one column; size/time are drawn into it (custom draw)
    InvalidateRect(g.hwnd, NULL, FALSE);
}

// ---------------------------------------------------------------------------
// Painting

static WString StepMeta(int step, COLORREF* color)
{
    *color = kMuted;
    if (step == 0) {
        const FileEntry* trc = FindFile(kTrcName);
        if (!trc) {
            *color = kWarn;
            return g.folderState == FOLDER_OK ? L"FSW_PSW.TRC NOT FOUND IN WORK"
                                              : L"WORK FOLDER NOT AVAILABLE";
        }
        return L"FSW_PSW.TRC / " + FormatThousands(trc->size) + L" B / " + FormatStamp(trc->modified);
    }
    if (step == 1) {
        const FileEntry* man = FindFile(kManName);
        if (!man)
            return L"FSW_PSW.MAN NOT FOUND IN WORK";
        if (man->size == 0) {
            *color = kGood;
            return L"FSW_PSW.MAN / EMPTY";
        }
        return L"FSW_PSW.MAN / " + FormatThousands(man->size) + L" B / " + FormatStamp(man->modified);
    }
    if (!g.canCompare)
        return L"AFTER YOU EDIT IN NCS DUMMY";
    if (!g.diffRead) {
        *color = kWarn;
        return L"COULDN'T READ FSW_PSW.TRC OR FSW_PSW.MAN";
    }
    const DiffSummary& s = g.diff;
    int oneSide = s.onlyMan + (s.partialMan ? 0 : s.onlyTrc);
    if (s.changed == 0 && oneSide == 0) {
        *color = kGood;
        return L"MAN MATCHES THE TRC / NO CHANGES";
    }
    *color = kAccent;
    wchar_t buf[96];
    if (oneSide)
        wsprintfW(buf, L"%d CHANGED / %d ONLY IN ONE FILE", s.changed, oneSide);
    else
        wsprintfW(buf, L"%d CHANGED FUNCTION%s", s.changed, s.changed == 1 ? L"" : L"S");
    return buf;
}

static void PaintStep(HDC dc, int i)
{
    const RECT& r = g.lay.rows[i];
    Fill(dc, r, kStepColors[i]);

    if (!g.lay.compact) {
        wchar_t number[4];
        wsprintfW(number, L"%02d", i + 1);
        RECT nr = { r.left + S(20), r.top, r.left + S(96), r.bottom };
        DrawLabel(dc, g_fonts.number, kNumber, number, nr, DT_LEFT | DT_VCENTER, -S(2));
    }

    RECT button = StepButtonRect(i);
    int textLeft = r.left + (g.lay.compact ? S(20) : S(100));
    RECT tr = { textLeft, r.top + S(24), button.left - S(16), r.top + S(50) };
    DrawLabel(dc, g_fonts.title, kWhite, kStepTitles[i], tr, DT_LEFT | DT_TOP);

    COLORREF metaColor;
    WString meta = StepMeta(i, &metaColor);
    RECT mr = { tr.left, r.top + S(56), tr.right, r.top + S(74) };
    DrawLabel(dc, g_fonts.label, metaColor, meta.c_str(), mr, DT_LEFT | DT_TOP, Tracking());
}

static void PaintNotice(HDC dc)
{
    const RECT& r = g.lay.notice;
    WString label, value;
    COLORREF labelColor = kFaint, valueColor = kWhite;
    if (g.lastBackup.empty()) {
        label = L"NO BACKUP YET";
        value = L"Backups are named CHASSIS-MODULE-DATE-TIME-FSW_PSW.TRC";
        valueColor = kSecondary;
        DashedFrame(dc, r, kBorder);
    } else if (g.backupJustNow) {
        wchar_t buf[48];
        wsprintfW(buf, g.backupWasMan ? L"MAN EMPTIED AT %02u:%02u" : L"BACKED UP AT %02u:%02u",
                  g.backupTime.wHour, g.backupTime.wMinute);
        label = buf;
        labelColor = kGood;
        value = g.backupWasMan ? L"Old MAN saved as " + FileNameOf(g.lastBackup) : FileNameOf(g.lastBackup);
        Frame(dc, r, kGood);
    } else {
        label = L"LAST BACKUP";
        value = FileNameOf(g.lastBackup);
        DashedFrame(dc, r, kBorder);
    }
    int textRight = g.lastBackup.empty() ? r.right - S(16) : NoticeLinkRect().left - S(12);
    RECT lr = { r.left + S(16), r.top + S(12), textRight, r.top + S(28) };
    RECT vr = { r.left + S(16), r.top + S(30), textRight, r.top + S(50) };
    DrawLabel(dc, g_fonts.label, labelColor, label.c_str(), lr, DT_LEFT | DT_TOP, Tracking());
    DrawLabel(dc, g_fonts.body, valueColor, value.c_str(), vr, DT_LEFT | DT_TOP);
}

static void PaintPathBlock(HDC dc, const RECT& block, const wchar_t* label, const WString& path,
                           const wchar_t* emptyText)
{
    RECT link = BlockLinkRect(block);
    RECT lr = { block.left, block.top, block.right, block.top + S(16) };  // the link sits below it
    RECT vr = { block.left, block.top + S(20), link.left - S(12), block.bottom };
    DrawLabel(dc, g_fonts.label, kFaint, label, lr, DT_LEFT | DT_TOP, Tracking());
    if (path.empty())
        DrawLabel(dc, g_fonts.body, kSecondary, emptyText, vr, DT_LEFT | DT_VCENTER);
    else
        DrawLabel(dc, g_fonts.body, kWhite, path.c_str(), vr, DT_LEFT | DT_VCENTER | DT_PATH_ELLIPSIS);
}

static void PaintMain(HDC dc, const RECT& client)
{
    const Layout& l = g.lay;
    Fill(dc, client, kPage);

    DrawLabel(dc, g_fonts.label, kFaint, L"CODING / 3 STEPS", l.header, DT_LEFT | DT_VCENTER, Tracking());
    for (int i = 0; i < kStepCount; ++i)
        PaintStep(dc, i);
    PaintNotice(dc);
    PaintPathBlock(dc, l.backupDir, L"BACKUP FOLDER", g.backupDir,
                   L"Not set. You'll choose one on your first backup.");

    Fill(dc, l.panel, kPanel);
    RECT divider = { l.panel.left, l.panel.top, l.panel.left + 1, l.panel.bottom };
    Fill(dc, divider, kRule);
    wchar_t head[48];
    COLORREF headColor = kFaint;
    if (g.folderState == FOLDER_OK) {
        wsprintfW(head, L"WORK / %02d", (int)g.files.size());
    } else {
        lstrcpyW(head, g.folderState == FOLDER_MISSING ? L"WORK / NOT FOUND" : L"WORK / CAN'T READ");
        headColor = kWarn;
    }
    DrawLabel(dc, g_fonts.label, headColor, head, l.panelHead, DT_LEFT | DT_VCENTER, Tracking());
    PaintPathBlock(dc, l.workDir, L"WORK FOLDER", g.workDir, L"");

    RECT rule = { l.footer.left, l.footer.top, l.footer.right, l.footer.top + 1 };
    Fill(dc, rule, kRule);
    wchar_t status[96];
    COLORREF statusColor = kFaint;
    UINT pollSec = kPollMs / 1000;
    int n = (int)g.files.size();
    if (g.folderState == FOLDER_MISSING) {
        wsprintfW(status, L"WORK FOLDER NOT FOUND / CHECKING EVERY %u S", pollSec);
        statusColor = kWarn;
    } else if (g.folderState == FOLDER_UNREADABLE) {
        wsprintfW(status, L"CAN'T READ THE WORK FOLDER / CHECKING EVERY %u S", pollSec);
        statusColor = kWarn;
    } else if (g.watch != INVALID_HANDLE_VALUE) {
        wsprintfW(status, L"%d FILE%s / UPDATES AUTOMATICALLY", n, n == 1 ? L"" : L"S");
    } else {
        wsprintfW(status, L"%d FILE%s / CHECKING EVERY %u S", n, n == 1 ? L"" : L"S", pollSec);
    }
    // Version on the right, so testers can tell builds apart.
    const wchar_t* version = L"V" APP_VERSION_STRING;
    RECT vr = { l.footer.right - S(20) - S(120), l.footer.top, l.footer.right - S(20), l.footer.bottom };
    DrawLabel(dc, g_fonts.label, kFaint, version, vr, DT_RIGHT | DT_VCENTER, Tracking());
    RECT sr = { l.footer.left + S(20), l.footer.top, vr.left - S(12), l.footer.bottom };
    DrawLabel(dc, g_fonts.label, statusColor, status, sr, DT_LEFT | DT_VCENTER, Tracking());
}

// ---------------------------------------------------------------------------
// File list

static void FillList()
{
    WString selected;
    int sel = ListView_GetNextItem(g.list, -1, LVNI_SELECTED);
    if (sel >= 0) {
        wchar_t buf[MAX_PATH];
        ListView_GetItemText(g.list, sel, 0, buf, MAX_PATH);
        selected = buf;
    }
    int top = ListView_GetTopIndex(g.list);

    SendMessageW(g.list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g.list);
    for (size_t i = 0; i < g.files.size(); ++i) {
        const FileEntry& f = g.files[i];
        LVITEMW item = {};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = (int)i;
        item.pszText = const_cast<wchar_t*>(f.name.c_str());
        item.lParam = (LPARAM)i;
        int idx = ListView_InsertItem(g.list, &item);
        if (f.name == selected)
            ListView_SetItemState(g.list, idx, LVIS_SELECTED | LVIS_FOCUSED,
                                  LVIS_SELECTED | LVIS_FOCUSED);
    }
    RECT rc;
    if (top > 0 && ListView_GetItemRect(g.list, 0, &rc, LVIR_BOUNDS))
        ListView_Scroll(g.list, 0, top * (rc.bottom - rc.top));
    SendMessageW(g.list, WM_SETREDRAW, TRUE, 0);
    FitListColumn(g.list);  // the scrollbar may have come or gone
    InvalidateRect(g.list, NULL, TRUE);
}

static LRESULT ListCustomDraw(NMLVCUSTOMDRAW* cd)
{
    int item = (int)cd->nmcd.dwItemSpec;
    size_t index = (size_t)cd->nmcd.lItemlParam;
    const FileEntry* f = index < g.files.size() ? &g.files[index] : NULL;
    switch (cd->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT: {
        if (!f)
            break;
        // Rows are drawn entirely here: the themed selection ignores our colours.
        HDC dc = cd->nmcd.hdc;
        UINT state = ListView_GetItemState(g.list, item, LVIS_SELECTED | LVIS_FOCUSED);
        bool selected = (state & LVIS_SELECTED) != 0;
        RECT rc;
        ListView_GetItemRect(g.list, item, &rc, LVIR_BOUNDS);
        Fill(dc, rc, selected ? kSelect : kPanel);
        // Size/time takes what it needs; the name gets the rest (narrow windows).
        WString text = FormatShortSize(f->size) + L"  " + FormatShortStamp(f->modified);
        int infoWidth = MeasureLabel(dc, g_fonts.label, text.c_str(), Tracking()) + Tracking();
        RECT info = { rc.right - S(12) - infoWidth, rc.top, rc.right - S(12), rc.bottom };
        RECT name = { rc.left + S(8), rc.top, info.left - S(12), rc.bottom };
        COLORREF nameColor = selected || IsKeyFile(f->name) ? kWhite : kSecondary;
        DrawLabel(dc, g_fonts.body, nameColor, f->name.c_str(), name, DT_LEFT | DT_VCENTER);
        DrawLabel(dc, g_fonts.label, selected ? kText : kMuted, text.c_str(), info,
                  DT_RIGHT | DT_VCENTER, Tracking());
        if ((state & LVIS_FOCUSED) && GetFocus() == g.list)
            Frame(dc, rc, kAccent);
        return CDRF_SKIPDEFAULT;
    }
    }
    return CDRF_DODEFAULT;
}

// ---------------------------------------------------------------------------
// State changes

// Rescans the folder; repaints only if something changed.
static void Refresh(bool force)
{
    std::vector<FileEntry> files;
    FolderState state = !DirExists(g.workDir)          ? FOLDER_MISSING
                        : ScanFiles(g.workDir, files) ? FOLDER_OK
                                                       : FOLDER_UNREADABLE;
    std::sort(files.begin(), files.end(), FileLess);

    bool wasWatching = g.watch != INVALID_HANDLE_VALUE;
    if (state != FOLDER_OK)
        StopWatch();  // folder deleted or renamed under us
    else if (!wasWatching)
        StartWatch();
    bool watchChanged = wasWatching != (g.watch != INVALID_HANDLE_VALUE);

    if (force || state != g.folderState || !SameFiles(files, g.files)) {
        g.files.swap(files);
        g.folderState = state;
        FillList();
        const FileEntry* trc = FindFile(kTrcName);
        const FileEntry* man = FindFile(kManName);
        g.canCompare = trc && man && man->size > 0;
        if (g.canCompare)
            g.diffRead = SummarizeDiff(JoinPath(g.workDir, kTrcName), JoinPath(g.workDir, kManName), &g.diff);
        EnableWindow(g.stepButtons[0], trc != NULL);
        EnableWindow(g.stepButtons[1], man && man->size > 0);
        EnableWindow(g.stepButtons[2], g.canCompare);
        InvalidateRect(g.hwnd, NULL, FALSE);
    } else if (watchChanged) {
        InvalidateRect(g.hwnd, &g.lay.footer, FALSE);
    }
}

static void UpdateBackupControls()
{
    SetWindowTextW(g.backupDirButton, g.backupDir.empty() ? L"CHOOSE" : L"CHANGE");
    ShowWindow(g.showBackup, g.lastBackup.empty() ? SW_HIDE : SW_SHOW);
    InvalidateRect(g.hwnd, NULL, FALSE);
}

static void SetWorkDir(const WString& raw)
{
    WString dir = NormalizeDir(raw);
    if (dir.empty() || lstrcmpiW(dir.c_str(), g.workDir.c_str()) == 0)
        return;
    g.workDir = dir;
    WriteSetting(L"WorkDir", dir);
    StopWatch();
    Refresh(true);
}

static bool ChooseBackupDir()
{
    WString dir = PickFolder(g.hwnd, L"Choose the folder where backups are saved.", g.backupDir);
    if (dir.empty())
        return false;
    g.backupDir = dir;
    WriteSetting(L"BackupDir", dir);
    UpdateBackupControls();
    return true;
}

static void RecordBackup(const WString& path, bool fromEmptyMan)
{
    g.lastBackup = path;
    g.backupJustNow = true;
    g.backupWasMan = fromEmptyMan;
    GetLocalTime(&g.backupTime);
    WriteSetting(L"LastBackup", path);
}

static void StartBackup()
{
    if (!FindFile(kTrcName))
        return;
    // No default backup folder: ask for one the first time.
    if ((g.backupDir.empty() || !DirExists(g.backupDir)) && !ChooseBackupDir())
        return;
    WString created;
    bool ok = RunBackupDialog(g.hwnd, JoinPath(g.workDir, kTrcName), g.backupDir, &created);
    if (ok)
        RecordBackup(created, false);
    UpdateBackupControls();  // the dialog may also have changed the folder
}

static void StartEmptyMan()
{
    const FileEntry* man = FindFile(kManName);
    if (!man || man->size == 0)
        return;
    // The MAN is always backed up first, so a backup folder is required.
    if ((g.backupDir.empty() || !DirExists(g.backupDir)) && !ChooseBackupDir())
        return;
    WString backup;
    bool ok = RunEmptyManDialog(g.hwnd, JoinPath(g.workDir, kManName), g.backupDir, &backup);
    if (ok) {
        RecordBackup(backup, true);
        Refresh(false);  // show the empty MAN now rather than after the watch fires
    }
    UpdateBackupControls();
}

// ---------------------------------------------------------------------------
// Window

static void CreateControls()
{
    g.stepButtons[0] = CreateThemedButton(g.hwnd, IDC_BACKUP, L"BACK UP", BUTTON_PRIMARY, kStepColors[0]);
    g.stepButtons[1] = CreateThemedButton(g.hwnd, IDC_EMPTY, L"EMPTY", BUTTON_SECONDARY, kStepColors[1]);
    g.stepButtons[2] = CreateThemedButton(g.hwnd, IDC_COMPARE, L"COMPARE", BUTTON_SECONDARY, kStepColors[2]);
    g.showBackup = CreateThemedButton(g.hwnd, IDC_SHOW_BACKUP, L"SHOW", BUTTON_LINK, kPage);
    g.backupDirButton = CreateThemedButton(g.hwnd, IDC_BACKUP_DIR, L"CHANGE", BUTTON_LINK, kPage);

    g.list = CreateWindowExW(0, WC_LISTVIEWW, L"",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_NOCOLUMNHEADER |
                                 LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                             0, 0, 0, 0, g.hwnd, (HMENU)(INT_PTR)IDC_FILES, GetModuleHandleW(NULL), NULL);
    ListView_SetExtendedListViewStyle(g.list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    ListView_SetBkColor(g.list, kPanel);
    ListView_SetTextBkColor(g.list, kPanel);
    ListView_SetTextColor(g.list, kText);
    SendMessageW(g.list, WM_SETFONT, (WPARAM)g_fonts.body, FALSE);
    // A 1px-wide image list is the XP-compatible way to set report row height.
    g.rowHeight = ImageList_Create(1, S(30), ILC_COLOR32, 1, 0);
    ListView_SetImageList(g.list, g.rowHeight, LVSIL_SMALL);
    LVCOLUMNW col = {};
    col.mask = LVCF_FMT | LVCF_WIDTH;
    col.fmt = LVCFMT_LEFT;
    col.cx = S(260);
    ListView_InsertColumn(g.list, 0, &col);
    ApplyDarkControl(g.list, L"DarkMode_Explorer");

    g.workDirButton = CreateThemedButton(g.hwnd, IDC_WORK_DIR, L"CHANGE", BUTTON_LINK, kPanel);
}

static void ClientToWindowSize(int cw, int ch, int* w, int* h)
{
    RECT rc = { 0, 0, S(cw), S(ch) };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    *w = rc.right - rc.left;
    *h = rc.bottom - rc.top;
}

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE:
        g.hwnd = hwnd;
        CreateControls();
        return 0;

    case WM_SIZE:
        LayoutChildren();
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        int w, h;
        ClientToWindowSize(620, 600, &w, &h);
        mmi->ptMinTrackSize.x = w;
        mmi->ptMinTrackSize.y = h;
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        PaintBuffered(hwnd, PaintMain);
        return 0;

    case WM_DRAWITEM:
        DrawThemedButton((const DRAWITEMSTRUCT*)lParam);
        return TRUE;

    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE)
            g.lastFocus = GetFocus();
        else
            SetFocus(g.lastFocus && IsWindowEnabled(g.lastFocus) ? g.lastFocus : g.list);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_BACKUP:
            StartBackup();
            return 0;
        case IDC_EMPTY:
            StartEmptyMan();
            return 0;
        case IDC_COMPARE:
            if (g.canCompare)
                RunCompareDialog(hwnd, JoinPath(g.workDir, kTrcName), JoinPath(g.workDir, kManName));
            return 0;
        case IDC_SHOW_BACKUP:
            ShowInExplorer(g.lastBackup);
            return 0;
        case IDC_BACKUP_DIR:
            ChooseBackupDir();
            return 0;
        case IDC_WORK_DIR: {
            WString dir = PickFolder(hwnd, L"Choose the NCS Expert WORK folder.", g.workDir);
            if (!dir.empty())
                SetWorkDir(dir);
            return 0;
        }
        case IDOK: {  // Enter, routed by IsDialogMessage: press the focused button
            HWND focus = GetFocus();
            wchar_t cls[16];
            if (focus && GetClassNameW(focus, cls, 16) && lstrcmpiW(cls, L"Button") == 0 &&
                IsWindowEnabled(focus))
                SendMessageW(focus, BM_CLICK, 0, 0);
            return 0;
        }
        }
        break;

    case WM_NOTIFY: {
        NMHDR* nm = (NMHDR*)lParam;
        if (nm->idFrom == IDC_FILES && nm->code == NM_CUSTOMDRAW)
            return ListCustomDraw((NMLVCUSTOMDRAW*)lParam);
        return 0;
    }

    case WM_TIMER:
        if (wParam == TIMER_REFRESH) {
            KillTimer(hwnd, TIMER_REFRESH);
            g.refreshPending = false;
        }
        Refresh(false);
        return 0;

    case WM_DESTROY:
        StopWatch();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void OnWatchSignaled()
{
    if (!FindNextChangeNotification(g.watch))
        StopWatch();  // the next poll restarts it
    if (!g.refreshPending) {
        g.refreshPending = true;
        SetTimer(g.hwnd, TIMER_REFRESH, kDebounceMs, NULL);
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icc);
    OleInitialize(NULL);  // SHBrowseForFolder with BIF_NEWDIALOGSTYLE needs OLE
    InitSettings();
    InitTheme();

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON),
                                   GetSystemMetrics(SM_CYSMICON), 0);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc))
        return 1;

    // Loading saved settings writes nothing.
    g.workDir = NormalizeDir(ReadSetting(L"WorkDir", kDefaultWorkDir));
    if (g.workDir.empty())
        g.workDir = kDefaultWorkDir;
    g.backupDir = NormalizeDir(ReadSetting(L"BackupDir"));
    g.lastBackup = ReadSetting(L"LastBackup");

    int w, h;
    ClientToWindowSize(960, 640, &w, &h);
    HWND hwnd = CreateWindowExW(0, kWindowClass, kAppTitle, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                CW_USEDEFAULT, CW_USEDEFAULT, w, h, NULL, NULL, hInstance, NULL);
    if (!hwnd)
        return 1;
    ApplyDarkFrame(hwnd);
    UpdateBackupControls();
    Refresh(true);
    SetTimer(hwnd, TIMER_POLL, kPollMs, NULL);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // No threads: wait on the change-notification handle and the message
    // queue together. Messages are pumped on every wake so a handle that
    // stays signaled can't starve them.
    MSG msg = {};
    bool quit = false;
    while (!quit) {
        DWORD count = g.watch != INVALID_HANDLE_VALUE ? 1 : 0;
        DWORD r = MsgWaitForMultipleObjects(count, &g.watch, FALSE, INFINITE, QS_ALLINPUT);
        if (count && r == WAIT_OBJECT_0)
            OnWatchSignaled();
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                quit = true;
                break;
            }
            if (!IsDialogMessageW(hwnd, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
    }

    ImageList_Destroy(g.rowHeight);
    ShutdownTheme();
    CloseSettings();
    OleUninitialize();
    return (int)msg.wParam;
}
