// Session dialogs. See sessiondlg.h.

#include <windows.h>
#include <shellapi.h>

#include "modal.h"
#include "sessiondlg.h"
#include "sha256.h"
#include "theme.h"
#include "util.h"

static const wchar_t kDialogClass[] = L"NcsWorkbenchSessionDialog";

enum { IDC_OK = 100, IDC_CANCEL_BUTTON, IDC_EMPTY_MAN = 112, IDC_OPTION = 120 };

const wchar_t* StageTitle(int stage)
{
    static const wchar_t* const titles[] = { L"",
                                             L"Prepare WORK",
                                             L"Read the ECU",
                                             L"Edit in NCS Dummy",
                                             L"Review changes",
                                             L"Code the ECU",
                                             L"Read back and verify",
                                             L"Finish" };
    return stage >= 0 && stage <= 7 ? titles[stage] : L"";
}

const wchar_t* StageTitle(SessionType type, int stage)
{
    if (type != SESSION_TYPE_VO)
        return StageTitle(stage);
    static const wchar_t* const titles[] = { L"", L"Prepare WORK", L"Read the ECU", L"Code from the VO",
                                             L"Read back", L"See what changed" };
    return stage >= 0 && stage <= 5 ? titles[stage] : L"";
}

static WString Upper(WString s)
{
    if (!s.empty())
        CharUpperBuffW(&s[0], (DWORD)s.size());
    return s;
}

WString SessionResultText(const Session& s)
{
    if (IsSessionOpen(s.state))
        return L"IN PROGRESS";
    if (s.type == SESSION_TYPE_VO && s.outcome == OUTCOME_UNVERIFIED)
        return L"NOT READ BACK";
    return OutcomeText(s.outcome);
}

COLORREF SessionResultColor(const Session& s)
{
    if (IsSessionOpen(s.state))
        return kAccent;
    switch (s.outcome) {
    case OUTCOME_VERIFIED:
    case OUTCOME_READ_BACK:
        return kGood;
    case OUTCOME_DISCREPANCIES:
    case OUTCOME_FAILED:
        return kBad;
    case OUTCOME_UNVERIFIED:
        return kWarn;
    default:
        return kMuted;
    }
}

WString SessionEcuText(const Session& s)
{
    return s.module.empty() ? WString(L"ECU NOT CONFIRMED") : s.daten + L" / " + s.module;
}

WString SessionTitleText(const Session& s)
{
    return s.type == SESSION_TYPE_VO ? SessionEcuText(s) + L" / VO" : SessionEcuText(s);
}

void OpenInNotepad(const WString& path)
{
    WString args = L"\"" + path + L"\"";
    ShellExecuteW(NULL, L"open", L"notepad.exe", args.c_str(), NULL, SW_SHOWNORMAL);
}

// ---------------------------------------------------------------------------
// A small frame shared by the dialogs here: paint and command callbacks,
// OK/Cancel buttons at the bottom right.

struct Dlg {
    HWND hwnd;
    bool done;
    int result;  // IDC_OK, IDC_CANCEL_BUTTON or IDCANCEL (Esc, close box)
    void (*paint)(Dlg* d, HDC dc, int w, int h);
    void (*command)(Dlg* d, int id, int code);  // NULL: OK and Cancel just close
    void* data;
    HWND ok, cancel;
    WString error;
};

static Dlg* g_painting;  // PaintBuffered takes a plain function

static void PaintThunk(HDC dc, const RECT& client)
{
    Fill(dc, client, kPage);
    g_painting->paint(g_painting, dc, client.right, client.bottom);
}

static void Close(Dlg* d, int result)
{
    d->result = result;
    d->done = true;
}

static LRESULT CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    Dlg* d = (Dlg*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_NCCREATE:
        d = (Dlg*)((CREATESTRUCTW*)lParam)->lpCreateParams;
        d->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)d);
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        g_painting = d;
        PaintBuffered(hwnd, PaintThunk);
        return 0;
    case WM_DRAWITEM:
        DrawThemedButton((const DRAWITEMSTRUCT*)lParam);
        return TRUE;
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDOK) {  // Enter presses the focused button
            HWND focus = GetFocus();
            wchar_t cls[16];
            if (focus && GetClassNameW(focus, cls, 16) && lstrcmpiW(cls, L"Button") == 0 && IsWindowEnabled(focus))
                SendMessageW(focus, BM_CLICK, 0, 0);
            return 0;
        }
        if (id == IDCANCEL) {
            Close(d, IDCANCEL);
            return 0;
        }
        if (d->command)
            d->command(d, id, HIWORD(wParam));
        else if (id == IDC_OK || id == IDC_CANCEL_BUTTON)
            Close(d, id);
        return 0;
    }
    case WM_CLOSE:
        Close(d, IDCANCEL);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static int ButtonTop(int h)
{
    return h - S(24) - S(44);
}

static void CreateButtons(Dlg* d, const wchar_t* okText, ButtonKind okKind, int okWidth, const wchar_t* cancelText,
                          int cancelWidth)
{
    RECT client;
    GetClientRect(d->hwnd, &client);
    int pad = S(24), by = ButtonTop(client.bottom);
    d->ok = CreateThemedButton(d->hwnd, IDC_OK, okText, okKind, kPage);
    RECT ok = { client.right - pad - S(okWidth), by, client.right - pad, by + S(44) };
    Place(d->ok, ok);
    if (cancelText && *cancelText) {
        d->cancel = CreateThemedButton(d->hwnd, IDC_CANCEL_BUTTON, cancelText, BUTTON_SECONDARY, kPage);
        RECT cancel = { ok.left - S(8) - S(cancelWidth), by, ok.left - S(8), by + S(44) };
        Place(d->cancel, cancel);
    }
}

static void PaintHeader(HDC dc, int w, const WString& label, const WString& title, const WString& line,
                        COLORREF lineColor)
{
    int pad = S(24);
    RECT r = { pad, S(24), w - pad, S(40) };
    DrawLabel(dc, g_fonts.label, kFaint, label.c_str(), r, DT_LEFT | DT_VCENTER, Tracking());
    r = { pad, S(44), w - pad, S(74) };
    DrawLabel(dc, g_fonts.heading, kWhite, title.c_str(), r, DT_LEFT | DT_VCENTER);
    if (!line.empty()) {
        r = { pad, S(78), w - pad, S(96) };
        DrawLabel(dc, g_fonts.label, lineColor, line.c_str(), r, DT_LEFT | DT_VCENTER | DT_PATH_ELLIPSIS,
                  Tracking());
    }
    RECT rule = { pad, S(110), w - pad, S(111) };
    Fill(dc, rule, kRule);
}

static void PaintFooter(Dlg* d, HDC dc, int w, int h, int errorTop)
{
    int pad = S(24), by = ButtonTop(h);
    if (!d->error.empty()) {
        RECT r = { pad, errorTop, w - pad, by - S(20) };
        DrawLabel(dc, g_fonts.body, kBad, d->error.c_str(), r, DT_LEFT | DT_TOP | DT_WORDBREAK);
    }
    RECT rule = { pad, by - S(16), w - pad, by - S(15) };
    Fill(dc, rule, kRule);
}

static int BodyHeight(const WString& text, int width)
{
    if (text.empty())
        return 0;
    HDC dc = GetDC(NULL);
    HGDIOBJ old = SelectObject(dc, g_fonts.body);
    RECT r = { 0, 0, width, 0 };
    DrawTextW(dc, text.c_str(), -1, &r, DT_LEFT | DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
    SelectObject(dc, old);
    ReleaseDC(NULL, dc);
    return r.bottom;
}

// File row: name and meta on the left, what happens on the right.
static void PaintFileRow(HDC dc, int left, int right, int top, const ConfirmRow& row, COLORREF actionColor)
{
    RECT r = { left, top + S(12), left + S(160), top + S(30) };
    DrawLabel(dc, g_fonts.mono, kWhite, row.name.c_str(), r, DT_LEFT | DT_VCENTER);
    r = { left, top + S(32), left + S(160), top + S(48) };
    WString meta = FitFileMeta(dc, row.exists, row.size, row.modified, r.right - r.left);
    DrawLabel(dc, g_fonts.label, kFaint, meta.c_str(), r, DT_LEFT | DT_VCENTER, Tracking());
    r = { left + S(176), top + S(12), right, top + S(56) };
    DrawLabel(dc, g_fonts.body, actionColor, row.action.c_str(), r, DT_LEFT | DT_TOP | DT_WORDBREAK);
    RECT rule = { left, top + S(60), right, top + S(61) };
    Fill(dc, rule, kRule);
}

WString FitFileMeta(HDC dc, bool exists, ULONGLONG size, const FILETIME& modified, int width)
{
    if (!exists)
        return L"NOT IN WORK";
    WString text;
    for (int fit = 0; fit < 4; ++fit) {
        WString age = FormatShortAge(modified, fit > 0);
        if (size == 0)
            text = fit < 3 ? L"EMPTY / " + age : WString(L"EMPTY");
        else if (fit < 2)
            text = FormatThousands(size) + L" B / " + age;
        else
            text = fit == 2 ? FormatShortSize(size) + L" / " + age : age;
        if (MeasureLabel(dc, g_fonts.label, text.c_str(), Tracking()) <= width)
            break;
    }
    return text;
}

static HWND OpenDialog(HWND owner, Dlg* d, const WString& caption, int w, int h)
{
    return CreateModal(owner, kDialogClass, DialogProc, caption.c_str(), S(w), S(h), d);
}

// ---------------------------------------------------------------------------
// Confirm and message

struct ConfirmData {
    const ConfirmSpec* spec;
    int bodyHeight;
};

static void PaintConfirm(Dlg* d, HDC dc, int w, int h)
{
    ConfirmData* c = (ConfirmData*)d->data;
    const ConfirmSpec& s = *c->spec;
    int pad = S(24);
    RECT r = { pad, S(24), w - pad, S(40) };
    DrawLabel(dc, g_fonts.label, kFaint, s.label.c_str(), r, DT_LEFT | DT_VCENTER, Tracking());
    r = { pad, S(44), w - pad, S(74) };
    DrawLabel(dc, g_fonts.heading, kWhite, s.title.c_str(), r, DT_LEFT | DT_VCENTER);
    int y = S(86);
    if (!s.body.empty()) {
        r = { pad, y, w - pad, y + c->bodyHeight };
        DrawLabel(dc, g_fonts.body, kSecondary, s.body.c_str(), r, DT_LEFT | DT_TOP | DT_WORDBREAK);
        y += c->bodyHeight + S(14);
    }
    if (!s.rows.empty()) {
        RECT rule = { pad, y, w - pad, y + 1 };
        Fill(dc, rule, kRule);
        for (size_t i = 0; i < s.rows.size(); ++i) {
            PaintFileRow(dc, pad, w - pad, y, s.rows[i], kSecondary);
            y += S(61);
        }
    }
    PaintFooter(d, dc, w, h, y + S(14));
}

bool RunConfirmDialog(HWND owner, const ConfirmSpec& spec)
{
    int w = 560;
    ConfirmData c = { &spec, BodyHeight(spec.body, S(w - 48)) };
    int h = 86 + MulDiv(c.bodyHeight, 96, S(96)) + 14 + (int)spec.rows.size() * 61 + 24 + 16 + 44 + 24 + 16;
    Dlg d = {};
    d.paint = PaintConfirm;
    d.data = &c;
    HWND hwnd = OpenDialog(owner, &d, spec.caption, w, h);
    if (!hwnd)
        return false;
    CreateButtons(&d, spec.okText.c_str(), spec.okKind, 180, spec.cancelText.c_str(), 160);
    RunModal(owner, hwnd, spec.focusOk || !d.cancel ? d.ok : d.cancel, &d.done);
    return d.result == IDC_OK;
}

void RunMessageDialog(HWND owner, const WString& caption, const WString& label, const WString& title,
                      const WString& body)
{
    ConfirmSpec spec;
    spec.caption = caption;
    spec.label = label;
    spec.title = title;
    spec.body = body;
    spec.okText = L"OK";
    spec.okKind = BUTTON_PRIMARY;
    spec.focusOk = true;
    RunConfirmDialog(owner, spec);
}

// ---------------------------------------------------------------------------
// Unfinished session

struct WorkFileState {
    WString name, status;
    bool exists;
    ULONGLONG size;
    FILETIME modified;
    COLORREF color;
};

static WorkFileState LookAtWorkFile(const Session& s, const wchar_t* name)
{
    WorkFileState f = {};
    f.name = name;
    f.color = kFaint;
    WString path = JoinPath(s.workDir, name);
    f.exists = ProbeFile(path, &f.size, &f.modified);
    if (!f.exists)
        return f;
    if (f.size == 0) {
        f.status = L"EMPTY";
        return f;
    }
    String data;
    if (!ReadFileBytes(path, &data)) {
        f.status = L"CAN'T READ IT";
        f.color = kWarn;
        return f;
    }
    String sha = Sha256Hex(data.data(), data.size());
    for (size_t i = 0; i < s.snaps.size(); ++i) {
        if (s.snaps[i].sha256 == sha) {
            f.status = L"SAVED IN THE SESSION";
            f.color = kGood;
            return f;
        }
    }
    f.status = L"NEW / LOOKED AT ON RESUME";
    f.color = kAccent;
    return f;
}

struct RecoverData {
    const Session* s;
    WorkFileState trc, man;
    WString line;
};

static void PaintRecover(Dlg* d, HDC dc, int w, int h)
{
    RecoverData* r = (RecoverData*)d->data;
    int pad = S(24);
    PaintHeader(dc, w, L"SESSION / " + r->s->id, L"Pick up where you left off?", r->line, kMuted);
    RECT t = { pad, S(126), w - pad, S(142) };
    DrawLabel(dc, g_fonts.label, kFaint, L"WORK NOW", t, DT_LEFT | DT_VCENTER, Tracking());
    int y = S(148);
    const WorkFileState* files[] = { &r->trc, &r->man };
    for (const WorkFileState* f : files) {
        RECT c = { pad, y, pad + S(130), y + S(36) };
        DrawLabel(dc, g_fonts.mono, kWhite, f->name.c_str(), c, DT_LEFT | DT_VCENTER);
        c = { pad + S(140), y, pad + S(300), y + S(36) };
        WString meta = FitFileMeta(dc, f->exists, f->size, f->modified, c.right - c.left);
        DrawLabel(dc, g_fonts.label, kFaint, meta.c_str(), c, DT_LEFT | DT_VCENTER, Tracking());
        c = { pad + S(300), y, w - pad, y + S(36) };
        DrawLabel(dc, g_fonts.label, f->color, f->status.c_str(), c, DT_RIGHT | DT_VCENTER, Tracking());
        RECT rule = { pad, y + S(36), w - pad, y + S(37) };
        Fill(dc, rule, kRule);
        y += S(37);
    }
    y += S(16);
    t = { pad, y, w - pad, y + S(60) };
    DrawLabel(dc, g_fonts.body, kSecondary,
              L"Nothing in WORK changes until you choose. Cancelling keeps every file the session saved and "
              L"marks it cancelled in history.",
              t, DT_LEFT | DT_TOP | DT_WORDBREAK);
    if (!r->s->error.empty()) {
        y += S(56);
        t = { pad, y, w - pad, y + S(16) };
        DrawLabel(dc, g_fonts.label, kBad, L"LAST ERROR", t, DT_LEFT | DT_VCENTER, Tracking());
        t = { pad, y + S(20), w - pad, ButtonTop(h) - S(20) };
        DrawLabel(dc, g_fonts.body, kText, r->s->error.c_str(), t, DT_LEFT | DT_TOP | DT_WORDBREAK);
    }
    PaintFooter(d, dc, w, h, h);
}

RecoverChoice RunRecoverDialog(HWND owner, const Session& s)
{
    RecoverData r;
    r.s = &s;
    r.trc = LookAtWorkFile(s, L"FSW_PSW.TRC");
    r.man = LookAtWorkFile(s, L"FSW_PSW.MAN");
    SYSTEMTIME last = s.started;
    if (!s.snaps.empty())
        last = s.snaps.back().taken;
    int stage = SessionStage(s);
    wchar_t stageText[8];
    wsprintfW(stageText, L"%02d", stage);
    r.line = SessionTitleText(s) + L" / STOPPED AT " + stageText + L" " + Upper(StageTitle(s.type, stage)) + L" / " +
             FormatDayAge(last);

    Dlg d = {};
    d.paint = PaintRecover;
    d.data = &r;
    HWND hwnd = OpenDialog(owner, &d, L"Unfinished session", 560, s.error.empty() ? 420 : 500);
    if (!hwnd)
        return RECOVER_RESUME;
    CreateButtons(&d, L"RESUME", BUTTON_PRIMARY, 160, L"CANCEL SESSION", 200);
    RunModal(owner, hwnd, d.ok, &d.done);
    return d.result == IDC_CANCEL_BUTTON ? RECOVER_CANCEL : RECOVER_RESUME;  // closing resumes: changes nothing
}

// ---------------------------------------------------------------------------
// Finish

static const Outcome kOptions[] = { OUTCOME_VERIFIED, OUTCOME_DISCREPANCIES, OUTCOME_UNVERIFIED, OUTCOME_FAILED };
static const wchar_t* const kOptionText[] = { L"Completed and verified", L"Completed with discrepancies",
                                              L"Completed without verification", L"Failed" };
static const Outcome kVoOptions[] = { OUTCOME_READ_BACK, OUTCOME_UNVERIFIED, OUTCOME_FAILED };
static const wchar_t* const kVoOptionText[] = { L"Coded and read back", L"Coded without a read-back", L"Failed" };
static const int kOptionCount = 4;

static int OptionCount(const Session& s)
{
    return s.type == SESSION_TYPE_VO ? 3 : kOptionCount;
}

static Outcome OptionOutcome(const Session& s, int i)
{
    return s.type == SESSION_TYPE_VO ? kVoOptions[i] : kOptions[i];
}

struct FinishData {
    Session* s;
    SessionVerification v;
    SessionChanges c;  // VO
    HWND options[kOptionCount];
    HWND emptyMan;
    bool manHasData;
    WString issue;
    COLORREF issueColor;
};

static WString OptionHint(const FinishData* f, Outcome o)
{
    const Session& s = *f->s;
    bool allowed = OutcomeAllowed(s, o);
    if (allowed && o == DerivedOutcome(s))
        return s.hasVerify ? L"FROM THE RESULTS" : L"NO READ-BACK";
    if (allowed)
        return o == OUTCOME_FAILED ? L"NCS EXPERT REPORTED AN ERROR" : L"";
    if (!s.hasVerify)
        return L"NEEDS A READ-BACK";
    if (o == OUTCOME_UNVERIFIED)
        return L"A READ-BACK EXISTS";
    wchar_t buf[64];
    if (s.verify.mismatch)
        wsprintfW(buf, L"NOT POSSIBLE: %d MISMATCH", s.verify.mismatch);
    else
        wsprintfW(buf, L"NOT POSSIBLE: %d CAN'T VERIFY", s.verify.unverifiable);
    return buf;
}

static void PaintFinish(Dlg* d, HDC dc, int w, int h)
{
    FinishData* f = (FinishData*)d->data;
    const Session& s = *f->s;
    int pad = S(24);
    WString ecu = s.module.empty() ? WString(L"ECU not confirmed") : s.daten + L" / " + s.module;
    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar_t changes[32];
    wsprintfW(changes, L" / %d CHANGE%s", s.changes, s.changes == 1 ? L"" : L"S");
    WString line = (s.vehicle.empty() ? WString() : Upper(s.vehicle) + L" / ") + FormatDay(s.started) + L" TO " +
                   FormatClock(now) + changes;
    bool vo = s.type == SESSION_TYPE_VO;
    PaintHeader(dc, w, vo ? L"05 / FINISH / VO CODING" : L"07 / FINISH", ecu, line, kMuted);

    int y = S(124);
    if (s.hasVerify) {
        int gap = S(2), tile = (w - 2 * pad - 2 * gap) / 3;
        const wchar_t* labels[] = { L"MATCH", L"MISMATCH", L"CAN'T VERIFY" };
        COLORREF colors[] = { kGood, kBad, kWarn };
        int counts[] = { s.verify.match, s.verify.mismatch, s.verify.unverifiable };
        if (vo) {
            labels[0] = L"CHANGED";
            labels[1] = L"ONLY BEFORE";
            labels[2] = L"ONLY AFTER";
            colors[0] = kAccent;
            colors[1] = kWarn;
            counts[0] = f->c.summary.changed;
            counts[1] = f->c.summary.onlyTrc;
            counts[2] = f->c.summary.onlyMan;
        }
        for (int i = 0; i < 3; ++i) {
            RECT t = { pad + i * (tile + gap), y, pad + i * (tile + gap) + tile, y + S(58) };
            Fill(dc, t, kTile1);
            RECT c = { t.left + S(14), t.top + S(10), t.right - S(8), t.top + S(26) };
            DrawLabel(dc, g_fonts.label, colors[i], labels[i], c, DT_LEFT | DT_VCENTER, Tracking());
            wchar_t n[16];
            wsprintfW(n, L"%d", counts[i]);
            c = { t.left + S(14), t.top + S(28), t.right - S(8), t.bottom - S(4) };
            DrawLabel(dc, g_fonts.heading, kWhite, n, c, DT_LEFT | DT_VCENTER);
        }
    } else {
        RECT t = { pad, y, w - pad, y + S(58) };
        DashedFrame(dc, t, kBorder);
        RECT c = { t.left + S(14), t.top + S(10), t.right - S(14), t.top + S(26) };
        DrawLabel(dc, g_fonts.label, kWarn, L"NO READ-BACK", c, DT_LEFT | DT_VCENTER, Tracking());
        c = { t.left + S(14), t.top + S(28), t.right - S(14), t.bottom - S(6) };
        DrawLabel(dc, g_fonts.body, kSecondary,
                  vo ? L"Nothing was read back after coding, so what changed isn't known."
                     : L"Nothing was read back after coding, so no value is verified.",
                  c, DT_LEFT | DT_VCENTER);
    }
    y += S(64);
    if (!f->issue.empty()) {
        RECT c = { pad, y, w - pad, y + S(20) };
        DrawLabel(dc, g_fonts.body, f->issueColor, f->issue.c_str(), c, DT_LEFT | DT_VCENTER);
    }
    y += S(28);

    RECT c = { pad, y, w - pad, y + S(16) };
    DrawLabel(dc, g_fonts.label, kFaint, L"SAVED IN THE SESSION", c, DT_LEFT | DT_VCENTER, Tracking());
    y += S(20);
    const Snapshot* files[] = { LastSnapshot(s, SNAP_READ), FindSnapshot(s, s.coded ? s.coded : s.reviewed),
                                LastSnapshot(s, SNAP_READBACK) };
    const wchar_t* kinds[] = { L"ORIGINAL", L"CODED MAN", L"READ-BACK" };
    if (vo) {  // no MAN: two rows
        kinds[0] = L"FIRST READ";
        kinds[1] = L"READ-BACK";
        files[1] = files[2];
    }
    for (int i = 0; i < (vo ? 2 : 3); ++i) {
        c = { pad, y, pad + S(110), y + S(22) };
        DrawLabel(dc, g_fonts.label, kFaint, kinds[i], c, DT_LEFT | DT_VCENTER, Tracking());
        c = { pad + S(120), y, w - pad, y + S(22) };
        if (files[i])
            DrawLabel(dc, g_fonts.mono, kWhite, files[i]->file.c_str(), c, DT_LEFT | DT_VCENTER);
        else
            DrawLabel(dc, g_fonts.label, kFaint, L"NONE", c, DT_LEFT | DT_VCENTER, Tracking());
        y += S(22);
    }
    if (vo)
        y += S(22);  // the options sit where they do for MAN

    y += S(14);
    c = { pad, y, w - pad, y + S(16) };
    DrawLabel(dc, g_fonts.label, kFaint, L"OUTCOME", c, DT_LEFT | DT_VCENTER, Tracking());
    y += S(20);
    for (int i = 0; i < OptionCount(s); ++i) {
        WString hint = OptionHint(f, OptionOutcome(s, i));
        bool allowed = OutcomeAllowed(s, OptionOutcome(s, i));
        COLORREF color = !allowed ? kDisabled : hint == L"FROM THE RESULTS" || hint == L"NO READ-BACK" ? kAccent : kFaint;
        c = { w - pad - S(240), y, w - pad, y + S(28) };
        DrawLabel(dc, g_fonts.label, color, hint.c_str(), c, DT_RIGHT | DT_VCENTER, Tracking());
        y += S(30);
    }
    y += S(10);
    if (!f->manHasData) {
        c = { pad, y, w - pad, y + S(20) };
        DrawLabel(dc, g_fonts.label, kGood, L"WORK / FSW_PSW.MAN IS EMPTY / NOTHING TO CLEAN UP", c,
                  DT_LEFT | DT_VCENTER, Tracking());
    }
    PaintFooter(d, dc, w, h, y + S(34));
}

static void FinishCommand(Dlg* d, int id, int code)
{
    FinishData* f = (FinishData*)d->data;
    (void)code;
    int count = OptionCount(*f->s);
    if (id >= IDC_OPTION && id < IDC_OPTION + count) {
        for (int i = 0; i < count; ++i)
            SetThemedCheck(f->options[i], i == id - IDC_OPTION);
    } else if (id == IDC_EMPTY_MAN) {
        SetThemedCheck(f->emptyMan, !IsThemedChecked(f->emptyMan));
    } else if (id == IDC_OK) {
        Outcome chosen = OUTCOME_NONE;
        for (int i = 0; i < count; ++i) {
            if (IsThemedChecked(f->options[i]))
                chosen = OptionOutcome(*f->s, i);
        }
        if (chosen == OUTCOME_NONE) {
            d->error = L"Pick an outcome first.";
            InvalidateRect(d->hwnd, NULL, FALSE);
            return;
        }
        WString e = FinishSession(f->s, chosen, f->emptyMan && IsThemedChecked(f->emptyMan));
        if (!e.empty()) {
            d->error = e;
            InvalidateRect(d->hwnd, NULL, FALSE);
            return;
        }
        Close(d, IDC_OK);
    } else if (id == IDC_CANCEL_BUTTON) {
        Close(d, id);
    }
}

bool RunFinishDialog(HWND owner, Session* s)
{
    FinishData f = {};
    f.s = s;
    LoadVerification(*s, &f.v);
    if (s->type == SESSION_TYPE_VO && LoadChanges(*s, &f.c)) {
        if (f.c.fewShared) {
            f.issue = L"The read-back shares few functions with the first read. Was it the same ECU?";
            f.issueColor = kWarn;
        } else if (f.c.changes == 0) {
            f.issue = L"The read-back matches the first read: the VO coding changed nothing.";
            f.issueColor = kText;
        }
    }
    for (size_t i = 0; f.v.loaded && i < f.v.rows.size() && f.issue.empty(); ++i) {
        const VerifyRow& row = f.v.rows[i];
        if (row.status == VERIFY_MISMATCH) {
            f.issue = Widen(row.name) + L" read back as " + Widen(JoinValues(row.readback)) + L", not " +
                      Widen(JoinValues(row.intended)) + L".";
            f.issueColor = kBad;
        }
    }
    for (size_t i = 0; f.v.loaded && i < f.v.rows.size() && f.issue.empty(); ++i) {
        if (f.v.rows[i].status == VERIFY_UNVERIFIABLE) {
            f.issue = Widen(f.v.rows[i].name) + L" couldn't be checked in the read-back.";
            f.issueColor = kWarn;
        }
    }
    ULONGLONG size;
    FILETIME modified;
    f.manHasData = ProbeFile(JoinPath(s->workDir, L"FSW_PSW.MAN"), &size, &modified) && size > 0;

    Dlg d = {};
    d.paint = PaintFinish;
    d.command = FinishCommand;
    d.data = &f;
    int w = 600, h = 640;
    HWND hwnd = OpenDialog(owner, &d, L"Finish the session", w, h);
    if (!hwnd)
        return false;
    int pad = S(24);
    // Rows as painted: header 124, tiles 64, issue 28, files 20 + 66, gap 14, label 20.
    int y = S(124) + S(64) + S(28) + S(20) + 3 * S(22) + S(14) + S(20);
    Outcome derived = DerivedOutcome(*s);
    for (int i = 0; i < OptionCount(*s); ++i) {
        const wchar_t* text = s->type == SESSION_TYPE_VO ? kVoOptionText[i] : kOptionText[i];
        f.options[i] = CreateThemedButton(hwnd, IDC_OPTION + i, text, BUTTON_CHECK, kPage);
        RECT r = { pad, y, S(w) - pad - S(250), y + S(28) };
        Place(f.options[i], r);
        EnableWindow(f.options[i], OutcomeAllowed(*s, OptionOutcome(*s, i)));
        SetThemedCheck(f.options[i], OptionOutcome(*s, i) == derived);
        y += S(30);
    }
    y += S(10);
    if (f.manHasData) {
        f.emptyMan = CreateThemedButton(hwnd, IDC_EMPTY_MAN, L"Empty FSW_PSW.MAN in WORK (saved first)", BUTTON_CHECK,
                                        kPage);
        RECT r = { pad, y - S(4), S(w) - pad, y + S(24) };
        Place(f.emptyMan, r);
        SetThemedCheck(f.emptyMan, true);
    }
    CreateButtons(&d, L"FINISH SESSION", BUTTON_PRIMARY, 200, L"BACK", 140);
    RunModal(owner, hwnd, d.ok, &d.done);
    return d.result == IDC_OK;
}
