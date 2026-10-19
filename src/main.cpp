// NCS Workbench - main window.
// Layout C: the coding column on the left, WORK folder files on the right.
// Idle, the column offers a guided session (session.h) and the three manual
// tools. During a session it lists the session's stages, the current one as
// a full tile with its instructions and actions.
// XP-era APIs only (_WIN32_WINNT=0x0501); see build.cmd for flags.

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <vector>

#include "backup.h"
#include "compare.h"
#include "history.h"
#include "man.h"
#include "resource.h"
#include "session.h"
#include "sessiondlg.h"
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
// During a session: how often to look at the file it waits for. A file is
// taken once it has been quiet for kSettleMs (session.h).
static const UINT kSessionPollMs = 500;
static const UINT WM_APP_ECU = WM_APP + 1;  // a trace was captured: ask which ECU it was

enum {
    IDC_BACKUP = 100,
    IDC_EMPTY,
    IDC_COMPARE,
    IDC_SHOW_BACKUP,
    IDC_BACKUP_DIR,
    IDC_FILES,
    IDC_WORK_DIR,
    IDC_START,
    IDC_HISTORY,
    IDC_CANCEL_SESSION,
    IDC_PRIMARY,
    IDC_SECONDARY,
    IDC_LINK1,  // IDC_LINK1 .. +2
    IDC_CHECK1 = IDC_LINK1 + 3,
};

enum { TIMER_REFRESH = 1, TIMER_POLL, TIMER_SESSION };

enum FolderState { FOLDER_OK, FOLDER_MISSING, FOLDER_UNREADABLE };

static const int kToolCount = 3;
static const wchar_t* const kToolTitles[kToolCount] = { L"Back up TRC", L"Empty MAN", L"Compare" };
static const wchar_t* const kToolButtons[kToolCount] = { L"BACK UP", L"EMPTY", L"COMPARE" };
static const COLORREF kToolColors[kToolCount] = { kTile1, kTile2, kTile3 };
static const int kStageCount = 7;
static const int kLinkCount = 3;
static const int kCheckCount = 5;
static const size_t kRecentCount = 3;

struct FileEntry {
    WString name;
    ULONGLONG size;
    FILETIME modified;
};

struct Layout {
    RECT header, panel, panelHead, list, workDir, footer, backupDir;
    int left, right;  // the coding column
    bool compact;     // narrow window: no big step numbers, smaller buttons
    // Idle
    RECT startTile, startButton, toolsLabel, tools[kToolCount], recentLabel, recent;
    // Session
    RECT stageRows[kStageCount + 1];  // by stage, empty when it's the tile
    RECT tile, number, title, status, body, error, buttons, primary, secondary;
};

struct App {
    HWND hwnd = NULL;
    HWND toolButtons[kToolCount] = {};
    HWND startButton = NULL, historyButton = NULL;
    HWND showBackup = NULL, backupDirButton = NULL, list = NULL, workDirButton = NULL;
    HWND cancelSession = NULL, primary = NULL, secondary = NULL;
    HWND links[kLinkCount] = {};
    HWND checks[kCheckCount] = {};
    HWND lastFocus = NULL;
    HIMAGELIST rowHeight = NULL;
    Layout lay = {};

    WString workDir, backupDir, lastBackup, dummyPath;
    bool backupJustNow = false;  // a backup was made in this run
    bool backupWasMan = false;   // ...by Empty MAN rather than Back up TRC
    SYSTEMTIME backupTime = {};

    std::vector<FileEntry> files;
    FolderState folderState = FOLDER_MISSING;
    // TRC vs MAN for the Compare tool's status line, recomputed when the files change.
    bool canCompare = false;  // both present, MAN not empty
    bool diffRead = false;
    DiffSummary diff;
    HANDLE watch = INVALID_HANDLE_VALUE;
    bool refreshPending = false;

    // Guided session; state SESSION_NONE when idle.
    Session session;
    HANDLE lock = NULL;  // LockWorkDir while a session is open
    std::vector<Session> recent;
    SessionState shownState = SESSION_NONE;
    SessionReview review;              // REVIEWING
    SessionVerification verification;  // VERIFYING
};

static App g;

static bool InSession()
{
    return IsSessionOpen(g.session.state);
}

static WString IntText(int n)
{
    wchar_t buf[16];
    wsprintfW(buf, L"%d", n);
    return buf;
}

static WString Plural(int n, const wchar_t* one, const wchar_t* many)
{
    return IntText(n) + L" " + (n == 1 ? one : many);
}

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
// Session state as shown

// Per state: the tile's buttons and links (NULL = hidden).
struct StateUi {
    const wchar_t* primary;
    int primaryWidth;
    const wchar_t* secondary;
    int secondaryWidth;
    const wchar_t* links[kLinkCount];
    bool checks;
};

static bool ReviewHasWarning();

static StateUi UiFor(SessionState state)
{
    StateUi u = {};
    switch (state) {
    case SESSION_PREPARING:
        u.primary = L"RETRY";
        u.primaryWidth = 150;
        break;
    case SESSION_AWAITING_READ:
        u.links[0] = L"CHECK NOW";
        break;
    case SESSION_AWAITING_ECU:
        u.primary = L"CONFIRM ECU";
        u.primaryWidth = 180;
        u.secondary = L"READ AGAIN";
        u.secondaryWidth = 160;
        break;
    case SESSION_AWAITING_MAN:
        u.primary = L"OPEN NCS DUMMY";
        u.primaryWidth = 200;
        u.links[0] = L"COPY PATH";
        u.links[1] = L"SHOW";
        u.links[2] = g.dummyPath.empty() ? L"CHOOSE" : L"CHANGE";
        break;
    case SESSION_REVIEWING:
        u.primary = L"READY TO CODE";
        u.primaryWidth = 180;
        u.secondary = L"COMPARE";
        u.secondaryWidth = 140;
        u.links[0] = ReviewHasWarning() ? L"RAW" : NULL;
        break;
    case SESSION_CODING:
        u.primary = L"I HAVE CODED";
        u.primaryWidth = 180;
        u.secondary = L"BACK TO REVIEW";
        u.secondaryWidth = 190;
        u.checks = true;
        break;
    case SESSION_CODED:
        u.primary = L"PREPARE READ-BACK";
        u.primaryWidth = 220;
        u.links[1] = L"FINISH WITHOUT READ-BACK";
        break;
    case SESSION_AWAITING_READBACK:
        u.links[0] = L"CHECK NOW";
        u.links[1] = L"FINISH WITHOUT READ-BACK";
        break;
    case SESSION_VERIFYING:
        u.primary = L"FINISH";
        u.primaryWidth = 150;
        u.secondary = L"READ BACK AGAIN";
        u.secondaryWidth = 190;
        break;
    default:
        break;
    }
    return u;
}

static void UpdateSessionCache()
{
    if (g.session.state == SESSION_REVIEWING)
        LoadReview(g.session, 0, &g.review);
    else
        g.review = SessionReview();
    if (g.session.state == SESSION_VERIFYING)
        LoadVerification(g.session, &g.verification);
    else
        g.verification = SessionVerification();
}

// A warning about the review's files: the MAN's first, else the trace's.
static WString CheckWarning(const TraceCheck& c, const wchar_t* file)
{
    if (c.binaryLines)
        return WString(file) + L" ISN'T PLAIN TEXT";
    if (!c.conflicting.empty())
        return Widen(c.conflicting[0]) + L" IS LISTED TWICE IN THE " + file + L" WITH DIFFERENT VALUES";
    int n = TraceIssueCount(c);
    if (!n)
        return WString();
    return Plural(n, L"LINE", L"LINES") + L" NOT UNDERSTOOD IN THE " + file + L" / FIRST AT LINE " +
           IntText(c.firstIssueLine);
}

static WString ReviewWarning()
{
    if (!g.review.loaded)
        return WString();
    if (!IsPlausibleTrace(g.review.man))
        return L"THE MAN CAN'T BE READ AS FUNCTIONS / CHECK IT RAW";
    WString w = CheckWarning(g.review.man, L"MAN");
    return w.empty() ? CheckWarning(g.review.trc, L"TRACE") : w;
}

static bool ReviewHasWarning()
{
    return !ReviewWarning().empty();
}

// Rows worth showing in the review: intended changes, and functions only in a full TRC.
static std::vector<size_t> ReviewRows()
{
    std::vector<size_t> rows;
    for (size_t i = 0; i < g.review.rows.size(); ++i) {
        if (IsNotable(g.review.rows[i], g.review.summary))
            rows.push_back(i);
    }
    return rows;
}

static WString StageDoneText(int stage)
{
    const Session& s = g.session;
    switch (stage) {
    case 1:
        return L"DONE " + FormatClock(s.prepared) + (s.reuseTrc ? L" / TRACE IN WORK USED" : L"");
    case 2: {
        const Snapshot* read = LastSnapshot(s, SNAP_READ);
        return read ? L"DONE " + FormatClock(read->taken) : WString(L"DONE");
    }
    case 3: {
        const Snapshot* man = LastSnapshot(s, SNAP_MAN);
        int n = CountSnapshots(s, SNAP_MAN);
        WString text = man ? L"EXPORTED " + FormatClock(man->taken) : WString(L"EXPORTED");
        return n > 1 ? text + L" / " + Plural(n, L"EXPORT", L"EXPORTS") : text;
    }
    case 4:
        return L"REVIEWED " + FormatClock(s.reviewedAt) + L" / " + Plural(s.changes, L"CHANGE", L"CHANGES");
    case 5:
        return L"YOU CONFIRMED " + FormatClock(s.codedAt);
    default:
        return WString();
    }
}

static WString SessionStatus(COLORREF* color)
{
    const Session& s = g.session;
    if (!s.notice.empty()) {
        *color = kWarn;
        return s.notice;
    }
    *color = kAccent;
    switch (s.state) {
    case SESSION_PREPARING:
        *color = s.error.empty() ? kFaint : kBad;
        return s.error.empty() ? L"SAVING AND CLEARING WORK" : L"COULDN'T FINISH PREPARING / STOPPED";
    case SESSION_AWAITING_READ:
        return L"WATCHING WORK FOR A NEW FSW_PSW.TRC";
    case SESSION_AWAITING_ECU: {
        const Snapshot* read = LastSnapshot(s, SNAP_READ);
        *color = kGood;
        return L"TRACE CAPTURED " + (read ? FormatClock(read->taken) : WString()) + L" / CONFIRM THE ECU";
    }
    case SESSION_AWAITING_MAN:
        return L"WAITING FOR AN EXPORTED FSW_PSW.MAN";
    case SESSION_REVIEWING: {
        if (!g.review.loaded) {
            *color = kBad;
            return L"COULDN'T READ THE SAVED TRACE OR MAN";
        }
        if (!IsPlausibleTrace(g.review.man)) {
            *color = kBad;
            return L"THE MAN CAN'T BE READ AS FUNCTIONS";
        }
        if (g.review.changes == 0) {
            *color = kGood;
            return L"MAN MATCHES THE TRACE / NO CHANGES";
        }
        const Snapshot* man = LastSnapshot(s, SNAP_MAN);
        int n = CountSnapshots(s, SNAP_MAN);
        return L"MAN EXPORT " + IntText(n) + L" OF " + IntText(n) + L" / " + FormatClock(man->taken) + L" / " +
               Plural(g.review.changes, L"CHANGED FUNCTION", L"CHANGED FUNCTIONS");
    }
    case SESSION_CODING:
        *color = kGood;
        return L"FSW_PSW.MAN IN WORK IS THE REVIEWED EXPORT";
    case SESSION_CODED:
        return L"CODING REPORTED " + FormatClock(s.codedAt) + L" / PREPARE THE READ-BACK";
    case SESSION_AWAITING_READBACK:
        return L"WATCHING WORK FOR THE READ-BACK FSW_PSW.TRC";
    case SESSION_VERIFYING: {
        const VerifySummary& v = s.verify;
        int total = v.match + v.mismatch + v.unverifiable;
        const Snapshot* rb = LastSnapshot(s, SNAP_READBACK);
        *color = v.mismatch ? kBad : v.unverifiable ? kWarn : kGood;
        return L"READ-BACK " + IntText(CountSnapshots(s, SNAP_READBACK)) + L" / " +
               (rb ? FormatClock(rb->taken) : WString()) + L" / " + IntText(v.match) + L" OF " + IntText(total) +
               L" MATCH";
    }
    default:
        return WString();
    }
}

static WString FooterStatus()
{
    switch (g.session.state) {
    case SESSION_PREPARING:
        return L"SESSION / PREPARING WORK";
    case SESSION_AWAITING_READ:
        return L"SESSION ACTIVE / WAITING FOR FSW_PSW.TRC";
    case SESSION_AWAITING_ECU:
        return L"SESSION ACTIVE / TRACE SAVED";
    case SESSION_AWAITING_MAN:
        return L"SESSION ACTIVE / WAITING FOR FSW_PSW.MAN";
    case SESSION_REVIEWING:
    case SESSION_CODING:
        return L"SESSION ACTIVE / WATCHING FSW_PSW.MAN FOR CHANGES";
    case SESSION_CODED:
        return L"SESSION ACTIVE / CODING REPORTED";
    case SESSION_AWAITING_READBACK:
        return L"SESSION ACTIVE / WAITING FOR THE READ-BACK";
    case SESSION_VERIFYING:
        return L"SESSION ACTIVE / READ-BACK SAVED";
    default:
        return WString();
    }
}

// ---------------------------------------------------------------------------
// Layout

static int TextWidth(HFONT font, const wchar_t* text, int tracking)
{
    HDC dc = GetDC(g.hwnd);
    int w = MeasureLabel(dc, font, text, tracking);
    ReleaseDC(g.hwnd, dc);
    return w;
}

// Link buttons draw their text right-aligned, so a link's rectangle is its text.
static int LinkWidth(HWND link)
{
    wchar_t text[64];
    GetWindowTextW(link, text, 64);
    return TextWidth(g_fonts.button, text, Tracking()) + Tracking() + S(2);
}

static RECT LeftLink(HWND link, int x, int cy)
{
    RECT r = { x, cy - S(12), x + LinkWidth(link), cy + S(12) };
    return r;
}

static RECT RightLink(HWND link, int right, int cy)
{
    RECT r = { right - LinkWidth(link), cy - S(12), right, cy + S(12) };
    return r;
}

static void ComputeSessionLayout()
{
    Layout& l = g.lay;
    for (int i = 0; i <= kStageCount; ++i)
        l.stageRows[i] = RECT();
    int stage = std::min(std::max(SessionStage(g.session.state), 1), 6);
    int bottom = l.footer.top - S(20);
    int y = S(48);
    for (int i = 1; i < stage; ++i) {
        l.stageRows[i] = { l.left, y, l.right, y + S(32) };
        y += S(34);
    }
    int tileBottom = std::max(bottom - (kStageCount - stage) * S(34), y + S(200));
    l.tile = { l.left, y, l.right, tileBottom };
    y = l.tile.bottom + S(2);
    for (int i = stage + 1; i <= kStageCount; ++i) {
        l.stageRows[i] = { l.left, y, l.right, y + S(32) };
        y += S(34);
    }

    int textLeft = l.tile.left + (l.compact ? S(20) : S(100));
    int textRight = l.tile.right - S(20);
    StateUi u = UiFor(g.session.state);
    bool topButton = g.session.state == SESSION_AWAITING_MAN;  // OPEN NCS DUMMY sits by the title
    int pw = S(l.compact ? std::min(u.primaryWidth, 170) : u.primaryWidth);
    int titleRight = topButton ? textRight - pw - S(16) : textRight;
    l.number = { l.tile.left + S(20), l.tile.top + S(18), l.tile.left + S(96), l.tile.top + S(64) };
    l.title = { textLeft, l.tile.top + S(20), titleRight, l.tile.top + S(46) };
    l.status = { textLeft, l.tile.top + S(52), titleRight, l.tile.top + S(68) };
    int by = l.tile.bottom - S(20) - S(44);
    l.buttons = { textLeft, by, textRight, by + S(44) };
    l.primary = topButton ? RECT{ textRight - pw, l.tile.top + S(20), textRight, l.tile.top + S(68) }
                          : RECT{ textRight - pw, by, textRight, by + S(44) };
    int sw = S(l.compact ? std::min(u.secondaryWidth, 160) : u.secondaryWidth);
    l.secondary = { l.primary.left - S(10) - sw, by, l.primary.left - S(10), by + S(44) };
    if (topButton)
        l.secondary = RECT();
    l.body = { textLeft, l.status.bottom + S(16), textRight, by - S(14) };
    l.error = RECT();
    if (!g.session.error.empty() && g.session.state != SESSION_PREPARING) {
        l.error = { textLeft, by - S(52), textRight, by - S(8) };
        l.body.bottom = l.error.top - S(6);
    }
}

static void ComputeLayout(int w, int h)
{
    Layout& l = g.lay;
    int pad = S(20);
    l.footer = { 0, h - S(34), w, h };
    // The files panel takes 30% of the width, between 200 and 280 px.
    int panelWidth = std::min(S(280), std::max(S(200), w * 3 / 10));
    l.panel = { w - panelWidth, 0, w, l.footer.top };
    l.left = pad;
    l.right = l.panel.left - pad;
    l.compact = l.right - l.left < S(470);
    l.header = { l.left, pad, l.right, pad + S(16) };
    l.backupDir = { l.left, l.footer.top - pad - S(44), l.right, l.footer.top - pad };

    l.startTile = { l.left, S(48), l.right, S(48) + S(176) };
    // Narrow: the button goes to the bottom right so the title keeps the full width.
    if (l.compact)
        l.startButton = { l.startTile.right - S(24) - S(170), l.startTile.bottom - S(24) - S(44),
                          l.startTile.right - S(24), l.startTile.bottom - S(24) };
    else
        l.startButton = { l.startTile.right - S(24) - S(184), l.startTile.top + S(28), l.startTile.right - S(24),
                          l.startTile.top + S(76) };
    int y = l.startTile.bottom + S(24);
    l.toolsLabel = { l.left, y, l.right, y + S(16) };
    y += S(26);
    int gap = S(2), tw = (l.right - l.left - 2 * gap) / kToolCount;
    for (int i = 0; i < kToolCount; ++i) {
        int x = l.left + i * (tw + gap);
        l.tools[i] = { x, y, i == kToolCount - 1 ? l.right : x + tw, y + S(96) };
    }
    y += S(96) + S(24);
    l.recentLabel = { l.left, y, l.right, y + S(16) };
    l.recent = { l.left, y + S(24), l.right, l.backupDir.top - S(12) };

    l.panelHead = { l.panel.left + S(16), pad, w - S(16), pad + S(16) };
    l.workDir = { l.panel.left + S(16), l.footer.top - pad - S(44), w - S(16), l.footer.top - pad };
    l.list = { l.panel.left + 1, S(48), w, l.workDir.top - S(16) };
    ComputeSessionLayout();
}

// "CHANGE"-style link in the top-right of a label + value block.
static RECT BlockLinkRect(const RECT& block)
{
    RECT b = { block.right - S(80), block.top + S(18), block.right, block.top + S(42) };
    return b;
}

static RECT ShowBackupRect()
{
    RECT change = BlockLinkRect(g.lay.backupDir);
    return RightLink(g.showBackup, change.left - S(20), (change.top + change.bottom) / 2);
}

// The box under the instructions that shows what WORK holds.
static RECT WatchBox(int lines)
{
    const RECT& b = g.lay.body;
    int top = b.top + lines * S(22) + S(10);
    RECT r = { b.left, top, b.right, top + S(52) };
    return r;
}

static int InstructionLines()
{
    switch (g.session.state) {
    case SESSION_AWAITING_READ:
        return 4;
    case SESSION_AWAITING_READBACK:
        return 3;
    default:
        return 0;
    }
}

// The review table and what follows it, inside the body.
struct ReviewGeometry {
    RECT header;
    int rowTop, shown, more;
    RECT summary, warning;
};

static ReviewGeometry ComputeReviewGeometry(int rowCount, bool warning)
{
    const RECT& b = g.lay.body;
    ReviewGeometry r = {};
    r.header = { b.left, b.top, b.right, b.top + S(26) };
    r.rowTop = r.header.bottom;
    int reserved = S(26) + (warning ? S(42) : 0);
    int fit = std::max(0, (int)(b.bottom - reserved - r.rowTop) / S(28));
    r.shown = std::min(rowCount, fit);
    if (r.shown < rowCount && r.shown > 0) {
        r.shown -= 1;  // room for the "+N MORE" line
    }
    r.more = rowCount - r.shown;
    int y = r.rowTop + r.shown * S(28) + (r.more ? S(28) : 0) + S(8);
    r.summary = { b.left, y, b.right, y + S(16) };
    y += S(26);
    r.warning = { b.left, y, b.right, y + S(32) };
    return r;
}

static void Show(HWND h, bool visible)
{
    ShowWindow(h, visible ? SW_SHOW : SW_HIDE);
}

static void LayoutChildren()
{
    if (!g.list)
        return;
    RECT rc;
    GetClientRect(g.hwnd, &rc);
    ComputeLayout(rc.right, rc.bottom);
    const Layout& l = g.lay;
    bool session = InSession();

    // Idle controls
    Show(g.startButton, !session);
    Show(g.historyButton, !session);
    Place(g.startButton, l.startButton);
    Place(g.historyButton, RightLink(g.historyButton, l.right, (l.header.top + l.header.bottom) / 2));
    for (int i = 0; i < kToolCount; ++i) {
        Show(g.toolButtons[i], !session);
        Place(g.toolButtons[i], LeftLink(g.toolButtons[i], l.tools[i].left + S(16), l.tools[i].bottom - S(24)));
    }
    Show(g.backupDirButton, !session);
    Show(g.showBackup, !session && !g.lastBackup.empty());
    Place(g.backupDirButton, BlockLinkRect(l.backupDir));
    Place(g.showBackup, ShowBackupRect());
    Show(g.workDirButton, !session);  // the session keeps its WORK folder
    Place(g.workDirButton, BlockLinkRect(l.workDir));

    // Session controls
    StateUi u = UiFor(g.session.state);
    Show(g.cancelSession, session);
    Place(g.cancelSession, RightLink(g.cancelSession, l.right, (l.header.top + l.header.bottom) / 2));
    Show(g.primary, session && u.primary);
    Show(g.secondary, session && u.secondary);
    if (u.primary)
        SetWindowTextW(g.primary, u.primary);
    if (u.secondary)
        SetWindowTextW(g.secondary, u.secondary);
    Place(g.primary, l.primary);
    Place(g.secondary, l.secondary);
    EnableWindow(g.primary, !(g.session.state == SESSION_REVIEWING &&
                              (!g.review.loaded || !IsPlausibleTrace(g.review.man))));
    for (int i = 0; i < kLinkCount; ++i) {
        Show(g.links[i], session && u.links[i]);
        if (u.links[i])
            SetWindowTextW(g.links[i], u.links[i]);
    }
    int buttonsCy = (l.buttons.top + l.buttons.bottom) / 2;
    switch (g.session.state) {
    case SESSION_AWAITING_READ:
    case SESSION_AWAITING_READBACK: {
        RECT box = WatchBox(InstructionLines());
        Place(g.links[0], RightLink(g.links[0], box.right - S(16), (box.top + box.bottom) / 2));
        Place(g.links[1], LeftLink(g.links[1], l.buttons.left, buttonsCy));
        break;
    }
    case SESSION_AWAITING_MAN: {
        int cy = l.body.top + S(32);
        RECT show = RightLink(g.links[1], l.body.right, cy);
        Place(g.links[1], show);
        Place(g.links[0], RightLink(g.links[0], show.left - S(20), cy));
        Place(g.links[2], RightLink(g.links[2], l.body.right, buttonsCy));
        break;
    }
    case SESSION_REVIEWING: {
        ReviewGeometry r = ComputeReviewGeometry((int)ReviewRows().size(), ReviewHasWarning());
        Place(g.links[0], RightLink(g.links[0], r.warning.right - S(14), (r.warning.top + r.warning.bottom) / 2));
        break;
    }
    case SESSION_CODED:
        Place(g.links[1], LeftLink(g.links[1], l.buttons.left, buttonsCy));
        break;
    default:
        break;
    }
    for (int i = 0; i < kCheckCount; ++i) {
        Show(g.checks[i], session && u.checks);
        RECT r = { l.body.left - S(2), l.body.top + i * S(26), l.body.right, l.body.top + i * S(26) + S(24) };
        Place(g.checks[i], r);
    }

    Place(g.list, l.list);
    FitListColumn(g.list);  // one column; size/time are drawn into it (custom draw)
    InvalidateRect(g.hwnd, NULL, FALSE);
}

// ---------------------------------------------------------------------------
// Painting: idle

static WString ToolMeta(int tool, COLORREF* color)
{
    *color = kMuted;
    if (tool == 0) {
        if (g.backupJustNow && !g.backupWasMan) {
            *color = kGood;
            return L"SAVED " + FormatClock(g.backupTime);
        }
        const FileEntry* trc = FindFile(kTrcName);
        if (!trc) {
            *color = kWarn;
            return g.folderState == FOLDER_OK ? L"NOT IN WORK" : L"NO WORK FOLDER";
        }
        return FormatThousands(trc->size) + L" B / " + FormatShortStamp(trc->modified);
    }
    if (tool == 1) {
        if (g.backupJustNow && g.backupWasMan) {
            *color = kGood;
            return L"EMPTIED " + FormatClock(g.backupTime);
        }
        const FileEntry* man = FindFile(kManName);
        if (!man)
            return L"NOT IN WORK";
        if (man->size == 0) {
            *color = kGood;
            return L"EMPTY";
        }
        return FormatThousands(man->size) + L" B / " + FormatShortStamp(man->modified);
    }
    if (!g.canCompare)
        return L"AFTER AN EDIT";
    if (!g.diffRead) {
        *color = kWarn;
        return L"CAN'T READ THE FILES";
    }
    const DiffSummary& s = g.diff;
    int oneSide = s.onlyMan + (s.partialMan ? 0 : s.onlyTrc);
    if (s.changed == 0 && oneSide == 0) {
        *color = kGood;
        return L"NO CHANGES";
    }
    *color = kAccent;
    if (oneSide)
        return IntText(s.changed) + L" CHANGED / " + IntText(oneSide) + L" ONLY IN ONE";
    return Plural(s.changed, L"CHANGED FUNCTION", L"CHANGED FUNCTIONS");
}

static void PaintPathBlock(HDC dc, const RECT& block, const wchar_t* label, const WString& path,
                           const wchar_t* emptyText, int valueRight)
{
    RECT lr = { block.left, block.top, block.right, block.top + S(16) };  // the link sits below it
    RECT vr = { block.left, block.top + S(20), valueRight, block.bottom };
    DrawLabel(dc, g_fonts.label, kFaint, label, lr, DT_LEFT | DT_TOP, Tracking());
    if (path.empty())
        DrawLabel(dc, g_fonts.body, kSecondary, emptyText, vr, DT_LEFT | DT_VCENTER);
    else
        DrawLabel(dc, g_fonts.body, kWhite, path.c_str(), vr, DT_LEFT | DT_VCENTER | DT_PATH_ELLIPSIS);
}

static void PaintIdle(HDC dc)
{
    const Layout& l = g.lay;
    DrawLabel(dc, g_fonts.label, kFaint, L"CODING / SESSIONS", l.header, DT_LEFT | DT_VCENTER, Tracking());

    // Start tile
    const RECT& t = l.startTile;
    Fill(dc, t, kTile1);
    int textRight = l.compact ? t.right - S(24) : l.startButton.left - S(24);
    RECT r = { t.left + S(24), t.top + S(24), textRight, t.top + S(54) };
    DrawLabel(dc, g_fonts.heading, kWhite, L"Start a coding session", r, DT_LEFT | DT_VCENTER);
    r = { t.left + S(24), t.top + S(62), textRight, l.compact ? l.startButton.top - S(8) : t.bottom - S(40) };
    if (g.folderState != FOLDER_OK)
        DrawLabel(dc, g_fonts.body, kWarn, L"The WORK folder isn't available. Check the WORK folder on the right.", r,
                  DT_LEFT | DT_TOP | DT_WORDBREAK);
    else if (l.compact)
        DrawLabel(dc, g_fonts.body, kSecondary,
                  L"Saves and clears the TRC and MAN, then guides you from the read to the read-back.", r,
                  DT_LEFT | DT_TOP | DT_WORDBREAK);
    else
        DrawLabel(dc, g_fonts.body, kSecondary,
                  L"Saves the current FSW_PSW.TRC and FSW_PSW.MAN, clears them for a fresh read, then walks you "
                  L"through reading, editing, coding and checking the ECU.",
                  r, DT_LEFT | DT_TOP | DT_WORDBREAK);
    if (!l.compact) {
        r = { t.left + S(24), t.bottom - S(36), t.right - S(24), t.bottom - S(20) };
        DrawLabel(dc, g_fonts.label, kDisabled,
                  L"01 PREPARE  02 READ  03 EDIT  04 REVIEW  05 CODE  06 VERIFY  07 FINISH", r, DT_LEFT | DT_VCENTER,
                  Tracking());
    }

    // Tools
    DrawLabel(dc, g_fonts.label, kFaint, L"TOOLS / OUTSIDE A SESSION", l.toolsLabel, DT_LEFT | DT_VCENTER, Tracking());
    for (int i = 0; i < kToolCount; ++i) {
        const RECT& tile = l.tools[i];
        Fill(dc, tile, kToolColors[i]);
        r = { tile.left + S(16), tile.top + S(14), tile.right - S(12), tile.top + S(34) };
        DrawLabel(dc, g_fonts.body, kWhite, kToolTitles[i], r, DT_LEFT | DT_VCENTER);
        COLORREF color;
        WString meta = ToolMeta(i, &color);
        r = { tile.left + S(16), tile.top + S(38), tile.right - S(12), tile.top + S(54) };
        DrawLabel(dc, g_fonts.label, color, meta.c_str(), r, DT_LEFT | DT_VCENTER, Tracking());
    }

    // Recent sessions
    DrawLabel(dc, g_fonts.label, kFaint, L"RECENT SESSIONS", l.recentLabel, DT_LEFT | DT_VCENTER, Tracking());
    int y = l.recent.top;
    if (g.recent.empty() && y + S(34) <= l.recent.bottom) {
        RECT rule = { l.left, y, l.right, y + 1 };
        Fill(dc, rule, kRule);
        r = { l.left, y, l.right, y + S(34) };
        DrawLabel(dc, g_fonts.body, kSecondary,
                  g.backupDir.empty() ? L"None yet. Sessions are saved in the backup folder."
                                      : L"None yet. Each session keeps its files in Sessions\\ in the backup folder.",
                  r, DT_LEFT | DT_VCENTER);
    }
    for (size_t i = 0; i < g.recent.size() && y + S(34) <= l.recent.bottom; ++i) {
        const Session& s = g.recent[i];
        RECT rule = { l.left, y, l.right, y + 1 };
        Fill(dc, rule, kRule);
        int resultWidth = S(l.compact ? 110 : 140);
        RECT c = { l.left, y, l.left + S(110), y + S(34) };
        DrawLabel(dc, g_fonts.label, kMuted, FormatDay(s.started).c_str(), c, DT_LEFT | DT_VCENTER, Tracking());
        c = { l.right - resultWidth, y, l.right, y + S(34) };
        DrawLabel(dc, g_fonts.label, SessionResultColor(s), SessionResultText(s).c_str(), c, DT_RIGHT | DT_VCENTER,
                  Tracking());
        int changesLeft = c.left - S(l.compact ? 0 : 104);
        if (!l.compact) {
            c = { changesLeft, y, changesLeft + S(96), y + S(34) };
            DrawLabel(dc, g_fonts.label, kMuted, Plural(s.changes, L"CHANGE", L"CHANGES").c_str(), c,
                      DT_LEFT | DT_VCENTER, Tracking());
        }
        c = { l.left + S(126), y, changesLeft - S(12), y + S(34) };
        DrawLabel(dc, g_fonts.mono, kWhite, SessionEcuText(s).c_str(), c, DT_LEFT | DT_VCENTER);
        y += S(34);
    }

    RECT link = g.lastBackup.empty() ? BlockLinkRect(l.backupDir) : ShowBackupRect();
    PaintPathBlock(dc, l.backupDir, L"BACKUP FOLDER / SESSIONS GO IN SESSIONS\\", g.backupDir,
                   L"Not set. You'll choose one on your first backup or session.", link.left - S(12));
}

// ---------------------------------------------------------------------------
// Painting: session

static void PaintSteps(HDC dc, const wchar_t* const* lines, int count, int top)
{
    const RECT& b = g.lay.body;
    for (int i = 0; i < count; ++i) {
        wchar_t n[4];
        wsprintfW(n, L"%d", i + 1);
        RECT r = { b.left, top + i * S(22), b.left + S(20), top + (i + 1) * S(22) };
        DrawLabel(dc, g_fonts.label, kFaint, n, r, DT_LEFT | DT_VCENTER, Tracking());
        r = { b.left + S(24), top + i * S(22), b.right, top + (i + 1) * S(22) };
        DrawLabel(dc, g_fonts.body, kText, lines[i], r, DT_LEFT | DT_VCENTER);
    }
}

static void PaintWatchBox(HDC dc, const wchar_t* name, const wchar_t* waiting, bool linkInside)
{
    RECT box = WatchBox(InstructionLines());
    DashedFrame(dc, box, kBorder);
    int right = linkInside ? box.right - S(16) - LinkWidth(g.links[0]) - S(16) : box.right - S(16);
    RECT r = { box.left + S(16), box.top + S(8), right, box.top + S(24) };
    DrawLabel(dc, g_fonts.label, kFaint, name, r, DT_LEFT | DT_VCENTER, Tracking());
    r = { box.left + S(16), box.top + S(26), right, box.bottom - S(6) };
    DrawLabel(dc, g_fonts.body, kSecondary, waiting, r, DT_LEFT | DT_VCENTER);
}

static WString TraceWaitText()
{
    const FileEntry* trc = FindFile(kTrcName);
    if (!trc)
        return L"Not in WORK yet.";
    if (!g.session.notice.empty())
        return L"There's an FSW_PSW.TRC, but it wasn't taken. Read the ECU again.";
    return L"Found. Waiting until NCS Expert has finished writing it.";
}

static void PaintNote(HDC dc, const wchar_t* text)
{
    // Left of the buttons, if there's room.
    const Layout& l = g.lay;
    int right = (IsWindowVisible(g.secondary) ? l.secondary.left : l.primary.left) - S(16);
    if (right - l.buttons.left < S(180))
        return;
    RECT r = { l.buttons.left, l.buttons.top, right, l.buttons.bottom };
    DrawLabel(dc, g_fonts.body, kSecondary, text, r, DT_LEFT | DT_VCENTER | DT_WORDBREAK);
}

static void PaintReview(HDC dc)
{
    const RECT& b = g.lay.body;
    if (!g.review.loaded)
        return;
    std::vector<size_t> rows = ReviewRows();
    WString warning = ReviewWarning();
    ReviewGeometry geo = ComputeReviewGeometry((int)rows.size(), !warning.empty());

    // Function | TRC | MAN, 46/27/27 like the compare window.
    int width = b.right - b.left;
    int trcLeft = b.left + width * 46 / 100, manLeft = b.left + width * 73 / 100;
    if (!rows.empty()) {
        RECT body = { b.left, geo.header.top, b.right, geo.rowTop + geo.shown * S(28) + (geo.more ? S(28) : 0) };
        Fill(dc, body, kTile3);
        RECT c = { b.left + S(14), geo.header.top, trcLeft - S(10), geo.header.bottom };
        DrawLabel(dc, g_fonts.label, kFaint, L"FUNCTION", c, DT_LEFT | DT_VCENTER, Tracking());
        c = { trcLeft, geo.header.top, manLeft - S(10), geo.header.bottom };
        DrawLabel(dc, g_fonts.label, kFaint, L"TRC / READ", c, DT_LEFT | DT_VCENTER, Tracking());
        c = { manLeft, geo.header.top, b.right - S(10), geo.header.bottom };
        DrawLabel(dc, g_fonts.label, kFaint, L"MAN / TO CODE", c, DT_LEFT | DT_VCENTER, Tracking());
        RECT rule = { b.left, geo.header.bottom - 1, b.right, geo.header.bottom };
        Fill(dc, rule, kRule);
    }
    for (int i = 0; i < geo.shown; ++i) {
        const DiffRow& row = g.review.rows[rows[i]];
        int y = geo.rowTop + i * S(28);
        RECT bar = { b.left, y, b.left + S(3), y + S(28) };
        Fill(dc, bar, row.status == DIFF_CHANGED ? kAccent : kWarn);
        RECT rule = { b.left, y + S(28) - 1, b.right, y + S(28) };
        Fill(dc, rule, kTile1);
        RECT c = { b.left + S(14), y, trcLeft - S(10), y + S(28) };
        DrawLabel(dc, g_fonts.mono, kWhite, Widen(row.name).c_str(), c, DT_LEFT | DT_VCENTER);
        c = { trcLeft, y, manLeft - S(10), y + S(28) };
        if (row.status == DIFF_ONLY_MAN)
            DrawLabel(dc, g_fonts.label, kWarn, L"NOT IN TRC", c, DT_LEFT | DT_VCENTER, Tracking());
        else
            DrawLabel(dc, g_fonts.mono, kText, Widen(JoinValues(row.trc)).c_str(), c, DT_LEFT | DT_VCENTER);
        c = { manLeft, y, b.right - S(10), y + S(28) };
        if (row.status == DIFF_ONLY_TRC)
            DrawLabel(dc, g_fonts.label, kWarn, L"NOT IN MAN", c, DT_LEFT | DT_VCENTER, Tracking());
        else
            DrawLabel(dc, g_fonts.mono, row.status == DIFF_CHANGED ? kAccent : kText, Widen(JoinValues(row.man)).c_str(),
                      c, DT_LEFT | DT_VCENTER);
    }
    if (geo.more) {
        int y = geo.rowTop + geo.shown * S(28);
        RECT c = { b.left + S(14), y, b.right - S(10), y + S(28) };
        WString more = L"+" + IntText(geo.more) + L" MORE / COMPARE SHOWS ALL";
        DrawLabel(dc, g_fonts.label, kMuted, more.c_str(), c, DT_LEFT | DT_VCENTER, Tracking());
    }

    const DiffSummary& s = g.review.summary;
    WString summary = IntText(s.changed) + L" CHANGED / " + IntText(s.onlyMan) + L" ONLY IN MAN";
    if (s.partialMan)
        summary += L" / PARTIAL MAN: " + IntText(g.review.man.functions) + L" OF " + IntText(g.review.trc.functions) +
                   L", THE REST ISN'T TOUCHED";
    else if (s.onlyTrc)
        summary += L" / " + IntText(s.onlyTrc) + L" ONLY IN TRC";
    if (rows.empty())
        geo.summary = { b.left, b.top, b.right, b.top + S(16) };
    DrawLabel(dc, g_fonts.label, kMuted, summary.c_str(), geo.summary, DT_LEFT | DT_VCENTER, Tracking());

    if (!warning.empty()) {
        DashedFrame(dc, geo.warning, kWarn);
        int right = geo.warning.right - S(14) - LinkWidth(g.links[0]) - S(14);
        RECT c = { geo.warning.left + S(14), geo.warning.top, right, geo.warning.bottom };
        DrawLabel(dc, g_fonts.label, kWarn, warning.c_str(), c, DT_LEFT | DT_VCENTER, Tracking());
    }
    PaintNote(dc, L"Export again from NCS Dummy any time; the newest export replaces this review.");
}

static void PaintVerification(HDC dc)
{
    const RECT& b = g.lay.body;
    if (!g.verification.loaded) {
        DrawLabel(dc, g_fonts.body, kBad, L"Couldn't read the saved files to compare. Check the session folder.", b,
                  DT_LEFT | DT_TOP | DT_WORDBREAK);
        return;
    }
    const std::vector<VerifyRow>& rows = g.verification.rows;
    ReviewGeometry geo = ComputeReviewGeometry((int)rows.size(), false);
    int width = b.right - b.left;
    int edges[4] = { b.left, b.left + width * 40 / 100, b.left + width * 60 / 100, b.left + width * 80 / 100 };
    if (rows.empty()) {
        RECT r = { b.left, b.top, b.right, b.top + S(22) };
        DrawLabel(dc, g_fonts.body, kSecondary, L"The coded MAN changed nothing, so there's nothing to verify.", r,
                  DT_LEFT | DT_VCENTER);
    } else {
        RECT body = { b.left, geo.header.top, b.right, geo.rowTop + geo.shown * S(28) + (geo.more ? S(28) : 0) };
        Fill(dc, body, kTile3);
        const wchar_t* heads[4] = { L"FUNCTION", L"TO CODE", L"READ BACK", L"RESULT" };
        for (int i = 0; i < 4; ++i) {
            RECT c = { i == 0 ? edges[0] + S(14) : edges[i], geo.header.top, i == 3 ? b.right - S(12) : edges[i + 1] - S(10),
                       geo.header.bottom };
            DrawLabel(dc, g_fonts.label, kFaint, heads[i], c, (i == 3 ? DT_RIGHT : DT_LEFT) | DT_VCENTER, Tracking());
        }
        RECT rule = { b.left, geo.header.bottom - 1, b.right, geo.header.bottom };
        Fill(dc, rule, kRule);
    }
    for (int i = 0; i < geo.shown; ++i) {
        const VerifyRow& row = rows[i];
        int y = geo.rowTop + i * S(28);
        COLORREF color = row.status == VERIFY_MATCH ? kGood : row.status == VERIFY_MISMATCH ? kBad : kWarn;
        RECT rule = { b.left, y + S(28) - 1, b.right, y + S(28) };
        Fill(dc, rule, kTile1);
        RECT c = { edges[0] + S(14), y, edges[1] - S(10), y + S(28) };
        DrawLabel(dc, g_fonts.mono, kWhite, Widen(row.name).c_str(), c, DT_LEFT | DT_VCENTER);
        c = { edges[1], y, edges[2] - S(10), y + S(28) };
        DrawLabel(dc, g_fonts.mono, kText, Widen(JoinValues(row.intended)).c_str(), c, DT_LEFT | DT_VCENTER);
        c = { edges[2], y, edges[3] - S(10), y + S(28) };
        if (row.readback.empty())
            DrawLabel(dc, g_fonts.label, kWarn, L"NOT IN TRACE", c, DT_LEFT | DT_VCENTER, Tracking());
        else
            DrawLabel(dc, g_fonts.mono, color, Widen(JoinValues(row.readback)).c_str(), c, DT_LEFT | DT_VCENTER);
        c = { edges[3], y, b.right - S(12), y + S(28) };
        const wchar_t* result = row.status == VERIFY_MATCH      ? L"MATCH"
                                : row.status == VERIFY_MISMATCH ? L"MISMATCH"
                                                                : L"CAN'T VERIFY";
        DrawLabel(dc, g_fonts.label, color, result, c, DT_RIGHT | DT_VCENTER, Tracking());
    }
    if (geo.more) {
        int y = geo.rowTop + geo.shown * S(28);
        RECT c = { b.left + S(14), y, b.right - S(10), y + S(28) };
        WString more = L"+" + IntText(geo.more) + L" MORE / SEE HISTORY";
        DrawLabel(dc, g_fonts.label, kMuted, more.c_str(), c, DT_LEFT | DT_VCENTER, Tracking());
    }
    PaintNote(dc, L"A match means the ECU holds the value. It doesn't prove the feature works in the car.");
}

static void PaintCheckMark(HDC dc, int x, int cy)
{
    HPEN pen = CreatePen(PS_SOLID, std::max(1, S(2) / 2 + 1), kGood);
    HGDIOBJ old = SelectObject(dc, pen);
    POINT pts[3] = { { x + S(1), cy }, { x + S(5), cy + S(4) }, { x + S(13), cy - S(4) } };
    Polyline(dc, pts, 3);
    SelectObject(dc, old);
    DeleteObject(pen);
}

static void PaintSessionBody(HDC dc)
{
    const Layout& l = g.lay;
    const RECT& b = l.body;
    const Session& s = g.session;
    switch (s.state) {
    case SESSION_PREPARING: {
        int y = b.top;
        if (!s.error.empty()) {
            RECT box = { b.left, y, b.right, y + S(96) };
            Frame(dc, box, kBad);
            RECT r = { box.left + S(16), box.top + S(12), box.right - S(16), box.bottom - S(10) };
            DrawLabel(dc, g_fonts.body, kText, s.error.c_str(), r, DT_LEFT | DT_TOP | DT_WORDBREAK);
            y = box.bottom + S(14);
        }
        RECT r = { b.left, y, b.right, b.bottom };
        DrawLabel(dc, g_fonts.body, kSecondary,
                  L"Nothing in WORK changes before its copy is saved in the session folder. Retry continues where "
                  L"it stopped; files already saved aren't saved twice.",
                  r, DT_LEFT | DT_TOP | DT_WORDBREAK);
        break;
    }
    case SESSION_AWAITING_READ: {
        static const wchar_t* const steps[] = {
            L"Open NCS Expert with your manipulation profile.",
            L"Choose the chassis and the ECU you want to code.",
            L"Run CODIERDATEN_LESEN.",
            L"Come back here. The trace is picked up on its own.",
        };
        PaintSteps(dc, steps, 4, b.top);
        PaintWatchBox(dc, L"FSW_PSW.TRC", TraceWaitText().c_str(), true);
        RECT r = { l.buttons.left, l.buttons.top, l.buttons.right, l.buttons.bottom };
        DrawLabel(dc, g_fonts.label, kDisabled, L"NEXT: CONFIRM THE ECU, THEN EDIT IN NCS DUMMY", r,
                  DT_LEFT | DT_VCENTER, Tracking());
        break;
    }
    case SESSION_AWAITING_ECU: {
        const Snapshot* read = LastSnapshot(s, SNAP_READ);
        WString text = L"The trace is saved in the session as " + (read ? read->file : WString()) +
                       L". Confirm which chassis and ECU it came from. The module suggestion comes from SP-Daten "
                       L"function names and can't prove which ECU wrote the trace.";
        DrawLabel(dc, g_fonts.body, kSecondary, text.c_str(), b, DT_LEFT | DT_TOP | DT_WORDBREAK);
        break;
    }
    case SESSION_AWAITING_MAN: {
        RECT r = { b.left, b.top, b.right, b.top + S(16) };
        DrawLabel(dc, g_fonts.label, kFaint, L"TRACE TO OPEN", r, DT_LEFT | DT_VCENTER, Tracking());
        const Snapshot* dummy = LastSnapshot(s, SNAP_DUMMY);
        RECT link;
        GetWindowRect(g.links[0], &link);
        MapWindowPoints(NULL, g.hwnd, (POINT*)&link, 2);
        r = { b.left, b.top + S(20), link.left - S(16), b.top + S(44) };
        DrawLabel(dc, g_fonts.mono, kWhite, dummy ? dummy->file.c_str() : L"", r, DT_LEFT | DT_VCENTER);
        static const wchar_t* const steps[] = {
            L"In NCS Dummy, load the trace above. It reads chassis and module from the name.",
            L"Change the functions you want.",
            L"Export the MAN to the WORK folder as FSW_PSW.MAN.",
        };
        PaintSteps(dc, steps, 3, b.top + S(56));
        int y = b.top + S(56) + 3 * S(22) + S(12);
        if (y + S(36) <= l.buttons.top - S(8)) {
            RECT box = { b.left, y, b.right, y + S(36) };
            DashedFrame(dc, box, kBorder);
            const FileEntry* man = FindFile(kManName);
            WString text = !man ? L"FSW_PSW.MAN / NOT IN WORK"
                                : man->size == 0 ? L"FSW_PSW.MAN / EMPTY / AN EMPTY MAN ISN'T AN EXPORT"
                                                 : L"FSW_PSW.MAN / " + FormatThousands(man->size) +
                                                       L" B / WAITING UNTIL IT'S WRITTEN";
            r = { box.left + S(16), box.top, box.right - S(16), box.bottom };
            DrawLabel(dc, g_fonts.label, kFaint, text.c_str(), r, DT_LEFT | DT_VCENTER, Tracking());
        }
        int linkLeft = l.buttons.right - LinkWidth(g.links[2]) - S(16);
        r = { l.buttons.left, l.buttons.top, l.buttons.left + S(96), l.buttons.bottom };
        DrawLabel(dc, g_fonts.label, kFaint, L"NCS DUMMY", r, DT_LEFT | DT_VCENTER, Tracking());
        r = { l.buttons.left + S(96), l.buttons.top, linkLeft, l.buttons.bottom };
        if (g.dummyPath.empty())
            DrawLabel(dc, g_fonts.body, kSecondary, L"Not set. You'll choose it on first use.", r, DT_LEFT | DT_VCENTER);
        else
            DrawLabel(dc, g_fonts.body, kSecondary, g.dummyPath.c_str(), r, DT_LEFT | DT_VCENTER | DT_PATH_ELLIPSIS);
        break;
    }
    case SESSION_REVIEWING:
        PaintReview(dc);
        break;
    case SESSION_CODING: {
        int y = b.top + kCheckCount * S(26);
        PaintCheckMark(dc, b.left, y + S(12));
        RECT r = { b.left + S(26), y, b.right, y + S(24) };
        WString text = L"Reviewed MAN is in WORK";
        DrawLabel(dc, g_fonts.body, kSecondary, text.c_str(), r, DT_LEFT | DT_VCENTER);
        int x = r.left + TextWidth(g_fonts.body, text.c_str(), 0) + S(12);
        r = { x, y, b.right, y + S(24) };
        DrawLabel(dc, g_fonts.label, kGood, L"CHECKED BY WORKBENCH", r, DT_LEFT | DT_VCENTER, Tracking());
        y += S(36);
        if (y + S(40) <= b.bottom) {
            r = { b.left, y, b.right, b.bottom };
            DrawLabel(dc, g_fonts.body, kSecondary,
                      L"Workbench never talks to the car. Click below once NCS Expert says SG_CODIEREN finished; "
                      L"it's recorded as your confirmation.",
                      r, DT_LEFT | DT_TOP | DT_WORDBREAK);
        }
        break;
    }
    case SESSION_CODED:
        DrawLabel(dc, g_fonts.body, kSecondary,
                  L"To check the coding, read the ECU again. Preparing saves FSW_PSW.TRC and FSW_PSW.MAN in the "
                  L"session, deletes the TRC and empties the MAN, so the read-back is clearly new and nothing gets "
                  L"coded twice by accident.",
                  b, DT_LEFT | DT_TOP | DT_WORDBREAK);
        break;
    case SESSION_AWAITING_READBACK: {
        static const wchar_t* const steps[] = {
            L"In NCS Expert, choose the same chassis and ECU.",
            L"Run CODIERDATEN_LESEN.",
            L"Come back here. The read-back is picked up on its own.",
        };
        PaintSteps(dc, steps, 3, b.top);
        PaintWatchBox(dc, L"FSW_PSW.TRC", TraceWaitText().c_str(), true);
        break;
    }
    case SESSION_VERIFYING:
        PaintVerification(dc);
        break;
    default:
        break;
    }
    if (l.error.right > l.error.left) {
        DrawLabel(dc, g_fonts.body, kBad, s.error.c_str(), l.error, DT_LEFT | DT_TOP | DT_WORDBREAK);
    }
}

static void PaintSession(HDC dc)
{
    const Layout& l = g.lay;
    const Session& s = g.session;
    WString head = L"SESSION / " + (s.module.empty() ? s.id : s.daten + L" / " + s.module);
    if (!s.vehicle.empty())
        head += L" / " + s.vehicle;
    RECT hr = l.header;
    hr.right -= LinkWidth(g.cancelSession) + S(16);
    DrawLabel(dc, g_fonts.label, kFaint, head.c_str(), hr, DT_LEFT | DT_VCENTER, Tracking());

    int stage = SessionStage(s.state);
    for (int i = 1; i <= kStageCount; ++i) {
        const RECT& r = l.stageRows[i];
        if (r.right == r.left)
            continue;
        bool done = i < stage;
        Fill(dc, r, kTile3);
        wchar_t n[4];
        wsprintfW(n, L"%02d", i);
        RECT c = { r.left + S(20), r.top, r.left + S(48), r.bottom };
        DrawLabel(dc, g_fonts.label, done ? kGood : kDisabled, n, c, DT_LEFT | DT_VCENTER, Tracking());
        WString status = done ? StageDoneText(i) : WString();
        int statusWidth = status.empty() ? 0 : TextWidth(g_fonts.label, status.c_str(), Tracking()) + S(8);
        statusWidth = std::min(statusWidth, (int)(r.right - r.left) / 2);
        c = { r.left + S(52), r.top, r.right - S(20) - statusWidth - S(8), r.bottom };
        DrawLabel(dc, g_fonts.body, done ? kText : kFaint, StageTitle(i), c, DT_LEFT | DT_VCENTER);
        if (!status.empty()) {
            c = { r.right - S(20) - statusWidth, r.top, r.right - S(20), r.bottom };
            DrawLabel(dc, g_fonts.label, kGood, status.c_str(), c, DT_RIGHT | DT_VCENTER, Tracking());
        }
    }

    Fill(dc, l.tile, kTile1);
    if (!l.compact) {
        wchar_t n[4];
        wsprintfW(n, L"%02d", std::max(stage, 1));
        DrawLabel(dc, g_fonts.number, kNumber, n, l.number, DT_LEFT | DT_TOP, -S(2));
    }
    const wchar_t* title = s.state == SESSION_CODING ? L"Code the ECU in NCS Expert" : StageTitle(stage);
    DrawLabel(dc, g_fonts.title, kWhite, title, l.title, DT_LEFT | DT_TOP);
    COLORREF color;
    WString status = SessionStatus(&color);
    DrawLabel(dc, g_fonts.label, color, status.c_str(), l.status, DT_LEFT | DT_TOP, Tracking());
    PaintSessionBody(dc);
}

// ---------------------------------------------------------------------------
// Painting: frame

static void PaintMain(HDC dc, const RECT& client)
{
    const Layout& l = g.lay;
    Fill(dc, client, kPage);
    if (InSession())
        PaintSession(dc);
    else
        PaintIdle(dc);

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
    RECT link = BlockLinkRect(l.workDir);
    PaintPathBlock(dc, l.workDir, L"WORK FOLDER", g.workDir, L"", link.left - S(12));
    if (InSession())
        DrawLabel(dc, g_fonts.label, kFaint, L"IN USE", link, DT_RIGHT | DT_VCENTER, Tracking());

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
    } else if (InSession()) {
        lstrcpynW(status, FooterStatus().c_str(), 96);
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
// Session flow

static void LoadRecent()
{
    g.recent.clear();
    if (g.backupDir.empty())
        return;
    g.recent = ListSessions(SessionsRoot(g.backupDir));
    if (g.recent.size() > kRecentCount)
        g.recent.resize(kRecentCount);
}

static void Message(const WString& title, const WString& body)
{
    RunMessageDialog(g.hwnd, kAppTitle, L"NCS WORKBENCH", title, body);
}

// Re-reads what the tile shows after the session changed.
static void SessionChanged()
{
    SessionState was = g.shownState;
    UpdateSessionCache();
    if (g.session.state == SESSION_CODING && was != SESSION_CODING) {
        for (int i = 0; i < kCheckCount; ++i)
            SetThemedCheck(g.checks[i], false);  // a fresh checklist for every coding attempt
    }
    g.shownState = g.session.state;
    if (g.session.state == SESSION_CODING) {
        WString same = L"Same car as the read";
        if (!g.session.vehicle.empty())
            same += L" (" + g.session.vehicle + L")";
        SetWindowTextW(g.checks[0], same.c_str());
        SetWindowTextW(g.checks[1], (L"Same ECU picked in NCS Expert: " + g.session.module).c_str());
    }
    LayoutChildren();
    if (was != SESSION_AWAITING_ECU && g.session.state == SESSION_AWAITING_ECU)
        PostMessageW(g.hwnd, WM_APP_ECU, 0, 0);
}

static void SessionTick()
{
    // Not while a dialog is up: it may be acting on the session.
    if (!InSession() || !IsWindowEnabled(g.hwnd))
        return;
    if (PollSession(&g.session, GetTickCount()))
        SessionChanged();
}

static void EnterSession()
{
    WriteSetting(L"ActiveSession", g.session.dir);
    g.shownState = SESSION_NONE;
    SetTimer(g.hwnd, TIMER_SESSION, kSessionPollMs, NULL);
    SessionChanged();
}

static void EndSession()
{
    KillTimer(g.hwnd, TIMER_SESSION);
    if (g.lock)
        CloseHandle(g.lock);
    g.lock = NULL;
    WriteSetting(L"ActiveSession", L"");
    InitSession(&g.session);
    g.shownState = SESSION_NONE;
    UpdateSessionCache();
    LoadRecent();
    LayoutChildren();
    SetFocus(g.startButton);
}

static bool ChooseBackupDir();
static void SetWorkDir(const WString& raw);
static void Refresh(bool force);

static void StartSession()
{
    if (InSession())
        return;
    if (g.folderState != FOLDER_OK) {
        Message(L"WORK folder not available",
                L"Workbench can't read " + g.workDir + L". Check the WORK folder setting on the right.");
        return;
    }
    // Sessions live in the backup folder: ask for one the first time.
    if ((g.backupDir.empty() || !DirExists(g.backupDir)) && !ChooseBackupDir())
        return;
    g.lock = LockWorkDir(g.workDir);
    if (!g.lock) {
        Message(L"WORK folder in use", L"Another NCS Workbench window is running a session on " + g.workDir +
                                           L". Finish or cancel it there first.");
        return;
    }
    bool reuse = false;
    if (!RunPrepareDialog(g.hwnd, g.workDir, g.backupDir, &reuse)) {
        CloseHandle(g.lock);
        g.lock = NULL;
        LayoutChildren();  // the backup folder may have changed
        return;
    }
    WString e = CreateSession(&g.session, SessionsRoot(g.backupDir), g.workDir, reuse);
    if (!e.empty()) {
        InitSession(&g.session);
        CloseHandle(g.lock);
        g.lock = NULL;
        Message(L"Couldn't start the session", e + L" Nothing in WORK was changed.");
        return;
    }
    PrepareWorkspace(&g.session);  // an error stays on the tile, with RETRY
    EnterSession();
    Refresh(true);
}

// At startup: a session that wasn't finished (crash, closed window, power loss).
static void CheckUnfinishedSession()
{
    WString dir = ReadSetting(L"ActiveSession");
    if (dir.empty())
        return;
    Session s;
    if (!LoadSession(dir, &s))
        return;  // folder unavailable (USB stick?): ask again next time
    if (!IsSessionOpen(s.state)) {
        WriteSetting(L"ActiveSession", L"");
        return;
    }
    if (RunRecoverDialog(g.hwnd, s) == RECOVER_CANCEL) {
        WString e = CancelSession(&s);
        WriteSetting(L"ActiveSession", L"");
        LoadRecent();
        LayoutChildren();
        if (!e.empty())
            Message(L"Session closed", L"The session was closed, but its record couldn't be updated. " + e);
        return;
    }
    HANDLE lock = LockWorkDir(s.workDir);
    if (!lock) {
        Message(L"Session open elsewhere", L"Another NCS Workbench window is running this session's WORK folder (" +
                                               s.workDir + L"). Continue it there.");
        return;
    }
    if (lstrcmpiW(s.workDir.c_str(), g.workDir.c_str()) != 0)
        SetWorkDir(s.workDir);  // the session keeps the WORK folder it started in
    g.session = s;
    g.lock = lock;
    EnterSession();
    Refresh(true);
}

static void CancelSessionFlow()
{
    ConfirmSpec spec;
    spec.caption = L"Cancel session";
    spec.label = L"SESSION / " + g.session.id;
    spec.title = L"Cancel this session?";
    spec.body = L"Every file the session saved stays in its folder, and it shows as cancelled in history. WORK isn't "
                L"changed: FSW_PSW.TRC and FSW_PSW.MAN keep what they have now.";
    spec.okText = L"CANCEL SESSION";
    spec.okKind = BUTTON_PRIMARY;
    spec.cancelText = L"KEEP GOING";
    spec.focusOk = false;
    if (!RunConfirmDialog(g.hwnd, spec))
        return;
    WString e = CancelSession(&g.session);
    EndSession();
    if (!e.empty())
        Message(L"Session closed", L"The session was closed, but its record couldn't be updated. " + e);
}

static ConfirmRow WorkRow(const wchar_t* name, const wchar_t* action, const wchar_t* missing)
{
    ConfirmRow row;
    row.name = name;
    ULONGLONG size;
    FILETIME modified;
    bool exists = ProbeFile(JoinPath(g.session.workDir, name), &size, &modified);
    row.meta = !exists ? WString(L"NOT IN WORK")
                       : size == 0 ? L"EMPTY / " + FormatShortStamp(modified)
                                   : FormatThousands(size) + L" B / " + FormatShortStamp(modified);
    row.action = exists && size > 0 ? action : missing;
    return row;
}

static void PrepareReadbackFlow()
{
    ConfirmSpec spec;
    spec.caption = L"Prepare the read-back";
    spec.label = L"06 / READ BACK";
    spec.title = L"Prepare the read-back";
    spec.body = L"Then run CODIERDATEN_LESEN for the same ECU in NCS Expert. Nothing else in WORK is touched.";
    spec.rows.push_back(WorkRow(kTrcName, L"Saved to the session folder, then deleted so the read-back is clearly new.",
                                L"Nothing to do."));
    spec.rows.push_back(WorkRow(kManName, L"Saved to the session folder, then emptied so nothing gets coded twice "
                                          L"by accident.",
                                L"Nothing to do."));
    spec.okText = L"SAVE AND CLEAR";
    spec.okKind = BUTTON_DANGER;
    spec.cancelText = L"CANCEL";
    spec.focusOk = false;
    if (!RunConfirmDialog(g.hwnd, spec))
        return;
    PrepareReadback(&g.session);
    SessionChanged();
    Refresh(true);
}

static void FinishFlow()
{
    if (RunFinishDialog(g.hwnd, &g.session)) {
        EndSession();
        Refresh(true);
    } else {
        SessionChanged();  // an error may have been recorded
    }
}

static void OpenEcuDialog()
{
    if (g.session.state != SESSION_AWAITING_ECU || !IsWindowEnabled(g.hwnd))
        return;
    const Snapshot* read = LastSnapshot(g.session, SNAP_READ);
    if (!read)
        return;
    WString path = SnapshotPath(g.session, *read);
    SYSTEMTIME taken = read->taken;
    String data;
    TraceCheck check;
    if (ReadFileBytes(path, &data))
        ScanTrace(data.data(), data.size(), &check);
    WString sha = Widen(read->sha256);
    CharUpperBuffW(&sha[0], (DWORD)sha.size());
    WString info = L"FSW_PSW.TRC / " + FormatThousands(read->size) + L" B / " +
                   Plural(check.functions, L"FUNCTION", L"FUNCTIONS") + L" / SHA-256 " + sha.substr(0, 6) + L".." +
                   sha.substr(sha.size() - 4);
    EcuChoice choice;
    choice.vehicle = g.session.vehicle;
    EcuResult r = RunEcuDialog(g.hwnd, path, g.session.workDir, L"02 / TRACE CAPTURED " + FormatClock(taken), info,
                               FormatFileStamp(taken), &choice);
    if (r == ECU_CONFIRMED)
        ConfirmEcu(&g.session, choice.chassis, choice.daten, choice.module, choice.vehicle);
    else if (r == ECU_READ_AGAIN)
        ReadAgain(&g.session);
    SessionChanged();
}

static bool ChooseNcsDummy()
{
    wchar_t file[MAX_PATH];
    lstrcpynW(file, g.dummyPath.c_str(), MAX_PATH);
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.hwnd;
    ofn.lpstrFilter = L"Programs (*.exe)\0*.exe\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Choose NCS Dummy";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn))
        return false;
    g.dummyPath = file;
    WriteSetting(L"NcsDummyPath", g.dummyPath);
    LayoutChildren();
    return true;
}

static void OpenNcsDummy()
{
    if ((g.dummyPath.empty() || !FileExists(g.dummyPath)) && !ChooseNcsDummy())
        return;
    // Started without arguments: no command line of NCS Dummy's is relied on.
    WString dir = g.dummyPath.substr(0, g.dummyPath.size() - FileNameOf(g.dummyPath).size());
    HINSTANCE r = ShellExecuteW(g.hwnd, L"open", g.dummyPath.c_str(), NULL, NormalizeDir(dir).c_str(), SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32)
        Message(L"Couldn't start NCS Dummy", L"Windows couldn't start " + g.dummyPath +
                                                 L". Check the path with CHANGE, or start NCS Dummy yourself.");
}

static void CopyToClipboard(const WString& text)
{
    if (!OpenClipboard(g.hwnd))
        return;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (mem) {
        memcpy(GlobalLock(mem), text.c_str(), bytes);
        GlobalUnlock(mem);
        if (!SetClipboardData(CF_UNICODETEXT, mem))
            GlobalFree(mem);
    }
    CloseClipboard();
}

static WString DummyTracePath()
{
    const Snapshot* dummy = LastSnapshot(g.session, SNAP_DUMMY);
    return dummy ? SnapshotPath(g.session, *dummy) : WString();
}

static void OnPrimary()
{
    Session& s = g.session;
    switch (s.state) {
    case SESSION_PREPARING:
        PrepareWorkspace(&s);
        SessionChanged();
        Refresh(true);
        break;
    case SESSION_AWAITING_ECU:
        OpenEcuDialog();
        break;
    case SESSION_AWAITING_MAN:
        OpenNcsDummy();
        break;
    case SESSION_REVIEWING:
        ReadyToCode(&s);
        SessionChanged();
        break;
    case SESSION_CODING:
        ConfirmCoded(&s);
        SessionChanged();
        break;
    case SESSION_CODED:
        PrepareReadbackFlow();
        break;
    case SESSION_VERIFYING:
        FinishFlow();
        break;
    default:
        break;
    }
}

static void OnSecondary()
{
    Session& s = g.session;
    switch (s.state) {
    case SESSION_AWAITING_ECU:
        ReadAgain(&s);
        SessionChanged();
        Refresh(true);
        break;
    case SESSION_REVIEWING: {
        const Snapshot* trc = LastSnapshot(s, SNAP_READ);
        const Snapshot* man = LastSnapshot(s, SNAP_MAN);
        if (trc && man)
            RunCompareDialog(g.hwnd, SnapshotPath(s, *trc), SnapshotPath(s, *man));
        break;
    }
    case SESSION_CODING:
        BackToReview(&s);
        SessionChanged();
        break;
    case SESSION_VERIFYING:
        PrepareReadbackFlow();
        break;
    default:
        break;
    }
}

static void OnLink(int i)
{
    Session& s = g.session;
    switch (s.state) {
    case SESSION_AWAITING_READ:
    case SESSION_AWAITING_READBACK:
        if (i == 0) {
            Refresh(true);
            SessionTick();
            InvalidateRect(g.hwnd, NULL, FALSE);
        } else {
            FinishFlow();
        }
        break;
    case SESSION_AWAITING_MAN:
        if (i == 0)
            CopyToClipboard(DummyTracePath());
        else if (i == 1)
            ShowInExplorer(DummyTracePath());
        else
            ChooseNcsDummy();
        break;
    case SESSION_REVIEWING: {
        // The raw files, side by side in Notepad, when the parse can't be trusted.
        const Snapshot* trc = LastSnapshot(s, SNAP_READ);
        const Snapshot* man = LastSnapshot(s, SNAP_MAN);
        if (trc)
            OpenInNotepad(SnapshotPath(s, *trc));
        if (man)
            OpenInNotepad(SnapshotPath(s, *man));
        break;
    }
    case SESSION_CODED:
        FinishFlow();
        break;
    default:
        break;
    }
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
        EnableWindow(g.toolButtons[0], trc != NULL);
        EnableWindow(g.toolButtons[1], man && man->size > 0);
        EnableWindow(g.toolButtons[2], g.canCompare);
        EnableWindow(g.startButton, state == FOLDER_OK);
        InvalidateRect(g.hwnd, NULL, FALSE);
    } else if (watchChanged) {
        InvalidateRect(g.hwnd, &g.lay.footer, FALSE);
    }
    SessionTick();
}

static void UpdateBackupControls()
{
    SetWindowTextW(g.backupDirButton, g.backupDir.empty() ? L"CHOOSE" : L"CHANGE");
    LayoutChildren();
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
    WString dir = PickFolder(g.hwnd, L"Choose the folder where backups and sessions are saved.", g.backupDir);
    if (dir.empty())
        return false;
    g.backupDir = dir;
    WriteSetting(L"BackupDir", dir);
    LoadRecent();
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
    if (!FindFile(kTrcName) || InSession())
        return;
    // No default backup folder: ask for one the first time.
    if ((g.backupDir.empty() || !DirExists(g.backupDir)) && !ChooseBackupDir())
        return;
    WString created;
    bool ok = RunBackupDialog(g.hwnd, JoinPath(g.workDir, kTrcName), g.backupDir, &created);
    if (ok)
        RecordBackup(created, false);
    LoadRecent();             // the dialog may have changed the folder
    UpdateBackupControls();
}

static void StartEmptyMan()
{
    const FileEntry* man = FindFile(kManName);
    if (!man || man->size == 0 || InSession())
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
    LoadRecent();
    UpdateBackupControls();
}

static void ShowHistory()
{
    if (g.backupDir.empty()) {
        Message(L"No sessions yet", L"Sessions are saved in the backup folder, and none is set yet. Start a session "
                                    L"to choose one.");
        return;
    }
    RunHistoryDialog(g.hwnd, SessionsRoot(g.backupDir));
    LoadRecent();
    InvalidateRect(g.hwnd, NULL, FALSE);
}

// ---------------------------------------------------------------------------
// Window

static void CreateControls()
{
    g.startButton = CreateThemedButton(g.hwnd, IDC_START, L"START SESSION", BUTTON_PRIMARY, kTile1);
    g.historyButton = CreateThemedButton(g.hwnd, IDC_HISTORY, L"HISTORY", BUTTON_LINK, kPage);
    for (int i = 0; i < kToolCount; ++i)
        g.toolButtons[i] = CreateThemedButton(g.hwnd, IDC_BACKUP + i, kToolButtons[i], BUTTON_LINK, kToolColors[i]);

    g.cancelSession = CreateThemedButton(g.hwnd, IDC_CANCEL_SESSION, L"CANCEL SESSION", BUTTON_LINK, kPage);
    for (int i = 0; i < kCheckCount; ++i)
        g.checks[i] = CreateThemedButton(g.hwnd, IDC_CHECK1 + i, L"", BUTTON_CHECK, kTile1);
    SetWindowTextW(g.checks[2], L"NCS Expert profile codes from FSW_PSW.MAN (manipulation)");
    SetWindowTextW(g.checks[3], L"Job SG_CODIEREN selected");
    SetWindowTextW(g.checks[4], L"Charger on, ignition on, cable stable");
    for (int i = 0; i < kLinkCount; ++i)
        g.links[i] = CreateThemedButton(g.hwnd, IDC_LINK1 + i, L"", BUTTON_LINK, kTile1);
    g.secondary = CreateThemedButton(g.hwnd, IDC_SECONDARY, L"", BUTTON_SECONDARY, kTile1);
    g.primary = CreateThemedButton(g.hwnd, IDC_PRIMARY, L"", BUTTON_PRIMARY, kTile1);

    g.showBackup = CreateThemedButton(g.hwnd, IDC_SHOW_BACKUP, L"SHOW LAST", BUTTON_LINK, kPage);
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
            SetFocus(g.lastFocus && IsWindowEnabled(g.lastFocus) && IsWindowVisible(g.lastFocus) ? g.lastFocus
                                                                                                 : g.list);
        return 0;

    case WM_APP_ECU:
        OpenEcuDialog();
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id >= IDC_LINK1 && id < IDC_LINK1 + kLinkCount) {
            OnLink(id - IDC_LINK1);
            return 0;
        }
        if (id >= IDC_CHECK1 && id < IDC_CHECK1 + kCheckCount) {
            HWND check = g.checks[id - IDC_CHECK1];
            SetThemedCheck(check, !IsThemedChecked(check));
            return 0;
        }
        switch (id) {
        case IDC_START:
            StartSession();
            return 0;
        case IDC_HISTORY:
            ShowHistory();
            return 0;
        case IDC_CANCEL_SESSION:
            CancelSessionFlow();
            return 0;
        case IDC_PRIMARY:
            OnPrimary();
            return 0;
        case IDC_SECONDARY:
            OnSecondary();
            return 0;
        case IDC_BACKUP:
            StartBackup();
            return 0;
        case IDC_EMPTY:
            StartEmptyMan();
            return 0;
        case IDC_COMPARE:
            if (g.canCompare && !InSession())
                RunCompareDialog(hwnd, JoinPath(g.workDir, kTrcName), JoinPath(g.workDir, kManName));
            return 0;
        case IDC_SHOW_BACKUP:
            ShowInExplorer(g.lastBackup);
            return 0;
        case IDC_BACKUP_DIR:
            ChooseBackupDir();
            return 0;
        case IDC_WORK_DIR: {
            if (InSession())
                return 0;
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
    }

    case WM_NOTIFY: {
        NMHDR* nm = (NMHDR*)lParam;
        if (nm->idFrom == IDC_FILES && nm->code == NM_CUSTOMDRAW)
            return ListCustomDraw((NMLVCUSTOMDRAW*)lParam);
        return 0;
    }

    case WM_TIMER:
        if (wParam == TIMER_SESSION) {
            SessionTick();
            return 0;
        }
        if (wParam == TIMER_REFRESH) {
            KillTimer(hwnd, TIMER_REFRESH);
            g.refreshPending = false;
        }
        Refresh(false);
        return 0;

    case WM_DESTROY:
        StopWatch();
        if (g.lock)
            CloseHandle(g.lock);  // the session stays open in its record; resumed on the next start
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
    g.dummyPath = ReadSetting(L"NcsDummyPath");
    InitSession(&g.session);
    LoadRecent();

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
    CheckUnfinishedSession();

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
