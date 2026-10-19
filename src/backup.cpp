// Backup TRC dialog. See backup.h.
//
// NCS Dummy reads chassis and module from file names shaped
// CHASSIS-MODULE[-text].TRC, where CHASSIS is the SP-Daten folder (E89 for
// an E91) and MODULE the module's coding file (MK60_87.C0A). With SP-Daten
// installed next to the WORK folder, the chassis list comes from it and the
// module is detected from the trace; without it, both are typed.

#include <windows.h>

#include <vector>

#include "backup.h"
#include "daten.h"
#include "modal.h"
#include "ncsdata.h"
#include "theme.h"
#include "util.h"

static const wchar_t kDialogClass[] = L"NcsWorkbenchBackup";

// Fallbacks when SP-Daten isn't found: chassis codes NCS Expert covers
// (E-series and the R-series MINI) and common coding modules.
static const wchar_t* const kChassis[] = {
    L"E36", L"E38", L"E39", L"E46", L"E52", L"E53", L"E60", L"E61", L"E63", L"E64",
    L"E65", L"E66", L"E67", L"E68", L"E70", L"E71", L"E72", L"E81", L"E82", L"E83",
    L"E84", L"E85", L"E86", L"E87", L"E88", L"E89", L"E90", L"E91", L"E92", L"E93",
    L"R55", L"R56", L"R57", L"R58", L"R59", L"R60", L"R61",
};
static const wchar_t* const kModules[] = {
    L"AHL", L"CAS", L"CCC", L"CIC", L"FRM", L"IHKA", L"JBBF", L"KOMBI", L"LM", L"M_ASK", L"PDC", L"SZL",
};

static const int kMaxCodeLength = 20;
static const size_t kMaxRecentModules = 8;
static const UINT WM_APP_REFRESH = WM_APP + 1;
static const UINT WM_APP_CHASSIS = WM_APP + 2;

enum { IDC_CHASSIS = 100, IDC_MODULE, IDC_CHANGE_FOLDER, IDC_DO_BACKUP, IDC_VEHICLE, IDC_READ_AGAIN };

static const int kMaxVehicleLength = 40;

enum Detection { DETECT_NONE, DETECT_FOUND, DETECT_UNCLEAR, DETECT_NO_TRACE };

struct BackupDialog {
    HWND hwnd;
    HWND chassis, module, changeFolder, cancel, ok;
    WString source;
    WString* backupDir;
    WString created;
    WString error;
    bool done;
    bool succeeded;

    // Session mode (RunEcuDialog): confirm the ECU instead of backing up.
    bool ecuMode;
    HWND vehicle;
    WString label, info, stamp;  // header label, trace line, capture time for the name
    EcuChoice* choice;
    bool readAgain;

    // SP-Daten
    WString datenDir;                                    // "" if not installed
    std::vector<std::pair<WString, WString> > chassisList;  // (chassis, folder)
    std::vector<TraceEntry> trace;
    bool traceRead;
    bool folderLoaded;
    WString folder;  // DATEN folder of the chosen chassis, "" if none
    Detection detection;
    int detectedHits, traceKnown;
};

// ---------------------------------------------------------------------------
// Names

// Upper case, A-Z 0-9 _ (and '.' in module file names): '-' separates the name's fields.
static WString SanitizeCode(const WString& raw, bool allowDot)
{
    WString out;
    for (size_t i = 0; i < raw.size() && (int)out.size() < kMaxCodeLength; ++i) {
        wchar_t c = raw[i];
        if (c >= L'a' && c <= L'z')
            c = wchar_t(c - L'a' + L'A');
        if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'_' || (allowDot && c == L'.'))
            out += c;
    }
    return out;
}

// CHASSIS-MODULE-YYYYMMDD-HHMM; CopyToUniqueName adds "-2" etc. after it
// when a backup of the same minute already exists.
static WString BackupStem(const WString& chassis, const WString& module, const SYSTEMTIME& t)
{
    return chassis + L"-" + module + L"-" + FormatFileStamp(t);
}

static const wchar_t kBackupTail[] = L"-FSW_PSW.TRC";

static std::vector<WString> SplitList(const WString& s)
{
    std::vector<WString> out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t comma = s.find(L',', start);
        if (comma == WString::npos)
            comma = s.size();
        WString item = SanitizeCode(s.substr(start, comma - start), true);
        if (!item.empty())
            out.push_back(item);
        start = comma + 1;
    }
    return out;
}

static void RememberModule(const WString& module)
{
    std::vector<WString> recent = SplitList(ReadSetting(L"RecentModules"));
    WString joined = module;
    size_t kept = 1;
    for (size_t i = 0; i < recent.size() && kept < kMaxRecentModules; ++i) {
        if (recent[i] != module) {
            joined += L"," + recent[i];
            ++kept;
        }
    }
    WriteSetting(L"RecentModules", joined);
}

// ---------------------------------------------------------------------------
// Chassis, modules and detection

static WString FolderForChassis(BackupDialog* d, const WString& chassis)
{
    for (size_t i = 0; i < d->chassisList.size(); ++i) {
        if (lstrcmpiW(d->chassisList[i].first.c_str(), chassis.c_str()) == 0)
            return d->chassisList[i].second;
    }
    return WString();
}

static void AddItem(HWND combo, const WString& text)
{
    if (SendMessageW(combo, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)text.c_str()) == CB_ERR)
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)text.c_str());
}

static void FillFallbackModules(BackupDialog* d)
{
    std::vector<WString> recent = SplitList(ReadSetting(L"RecentModules"));
    for (size_t i = 0; i < recent.size(); ++i)
        AddItem(d->module, recent[i]);
    for (const wchar_t* m : kModules)
        AddItem(d->module, m);
}

// Called when the chassis changes: reload the folder's modules and pick the
// one the trace came from.
static void UpdateForChassis(BackupDialog* d)
{
    WString chassis = SanitizeCode(GetWindowTextString(d->chassis), false);
    WString folder = FolderForChassis(d, chassis);
    if (d->folderLoaded && lstrcmpiW(folder.c_str(), d->folder.c_str()) == 0)
        return;
    d->folder = folder;
    d->folderLoaded = true;
    d->detection = DETECT_NONE;

    WString typed = GetWindowTextString(d->module);
    SendMessageW(d->module, CB_RESETCONTENT, 0, 0);  // also clears the edit text
    if (folder.empty()) {
        FillFallbackModules(d);
        SetWindowTextW(d->module, typed.c_str());
        InvalidateRect(d->hwnd, NULL, FALSE);
        return;
    }

    HCURSOR old = SetCursor(LoadCursorW(NULL, IDC_WAIT));
    WString path = JoinPath(d->datenDir, folder);
    std::vector<DatenModule> modules = LoadModules(path);
    std::vector<unsigned> traceIds = TraceFunctionIds(path, d->trace);
    std::vector<int> hits;
    for (size_t i = 0; i < modules.size(); ++i) {
        AddItem(d->module, modules[i].name);
        hits.push_back(CountSharedFunctions(traceIds, modules[i].functions));
    }
    SetCursor(old);

    // Measured against all of the trace's functions, not just the ones this SP-Daten
    // knows: a mismatched SP-Daten version mustn't "detect" from a couple of names.
    int pick = PickModule(hits, (int)d->trace.size());
    if (!d->traceRead) {
        d->detection = DETECT_NO_TRACE;
    } else if (pick >= 0) {
        d->detection = DETECT_FOUND;
        d->detectedHits = hits[pick];
        d->traceKnown = (int)d->trace.size();
        typed = modules[pick].name;
    } else {
        d->detection = DETECT_UNCLEAR;
    }
    SetWindowTextW(d->module, typed.c_str());
    InvalidateRect(d->hwnd, NULL, FALSE);
}

// ---------------------------------------------------------------------------
// Dialog

struct Rects {
    RECT chassis, module, change, cancel, ok;
    RECT vehicleBox, vehicle, note;  // session mode
};

static Rects DialogRects(int w, int h)
{
    int pad = S(24);
    int col = (w - 2 * pad - S(16)) / 2;
    Rects r;
    r.chassis = { pad, S(146), pad + col, S(146) + S(300) };  // height includes the drop-down list
    r.module = { pad + col + S(16), S(146), w - pad, S(146) + S(300) };
    r.change = { w - pad - S(80), S(234), w - pad, S(258) };
    int by = h - pad - S(44);
    r.ok = { w - pad - S(140), by, w - pad, by + S(44) };
    r.cancel = { r.ok.left - S(8) - S(140), by, r.ok.left - S(8), by + S(44) };
    r.vehicleBox = { pad, S(238), w - pad, S(270) };
    r.vehicle = { pad + S(10), S(245), w - pad - S(10), S(264) };
    r.note = { pad, S(284), w - pad, S(342) };
    return r;
}

// The name's two codes: the DATEN folder when the chassis is known to SP-Daten, else as typed.
static bool ReadCodes(BackupDialog* d, WString* chassis, WString* module)
{
    WString typed = SanitizeCode(GetWindowTextString(d->chassis), false);
    WString folder = FolderForChassis(d, typed);
    *chassis = folder.empty() ? typed : SanitizeCode(folder, false);
    *module = SanitizeCode(GetWindowTextString(d->module), true);
    return !chassis->empty() && !module->empty();
}

static BackupDialog* g_painting;  // PaintBuffered takes a plain function

static void PaintFieldInfo(HDC dc, BackupDialog* d, const Rects& rc)
{
    // Under the fields: where the chassis' data lives, and what the trace matched.
    RECT ci = { rc.chassis.left, S(188), rc.chassis.right, S(204) };
    RECT mi = { rc.module.left, S(188), rc.module.right, S(204) };
    WString typedChassis = SanitizeCode(GetWindowTextString(d->chassis), false);
    if (d->datenDir.empty()) {
        DrawLabel(dc, g_fonts.label, kFaint, L"SP-DATEN NOT FOUND", ci, DT_LEFT | DT_VCENTER, Tracking());
    } else if (!d->folder.empty()) {
        WString where = L"DATEN\\" + d->folder;
        DrawLabel(dc, g_fonts.label, kMuted, where.c_str(), ci, DT_LEFT | DT_VCENTER, Tracking());
    } else if (!typedChassis.empty()) {
        DrawLabel(dc, g_fonts.label, kWarn, L"NOT IN SP-DATEN", ci, DT_LEFT | DT_VCENTER, Tracking());
    }
    if (d->detection == DETECT_FOUND) {
        // In a session it's only a suggestion: names can't prove which ECU wrote the trace.
        wchar_t text[64];
        wsprintfW(text, d->ecuMode ? L"SUGGESTED FROM TRACE (%d/%d)" : L"DETECTED FROM TRACE (%d/%d)",
                  d->detectedHits, d->traceKnown);
        DrawLabel(dc, g_fonts.label, d->ecuMode ? kAccent : kGood, text, mi, DT_LEFT | DT_VCENTER, Tracking());
    } else if (d->detection == DETECT_UNCLEAR) {
        DrawLabel(dc, g_fonts.label, kWarn, L"NO CLEAR MATCH, PICK ONE", mi, DT_LEFT | DT_VCENTER, Tracking());
    } else if (d->detection == DETECT_NO_TRACE) {
        DrawLabel(dc, g_fonts.label, kWarn, L"COULDN'T READ THE TRACE", mi, DT_LEFT | DT_VCENTER, Tracking());
    }
}

static void PaintEcuDialog(HDC dc, BackupDialog* d, int w)
{
    int pad = S(24);
    RECT r = { pad, S(24), w - pad, S(40) };
    DrawLabel(dc, g_fonts.label, kFaint, d->label.c_str(), r, DT_LEFT | DT_VCENTER, Tracking());
    r = { pad, S(44), w - pad, S(74) };
    DrawLabel(dc, g_fonts.heading, kWhite, L"Which ECU was this?", r, DT_LEFT | DT_VCENTER);
    r = { pad, S(78), w - pad, S(96) };
    DrawLabel(dc, g_fonts.label, kMuted, d->info.c_str(), r, DT_LEFT | DT_VCENTER, Tracking());
    RECT rule = { pad, S(110), w - pad, S(111) };
    Fill(dc, rule, kRule);

    RECT client;
    GetClientRect(d->hwnd, &client);
    Rects rc = DialogRects(client.right, client.bottom);
    r = { rc.chassis.left, S(124), rc.chassis.right, S(140) };
    DrawLabel(dc, g_fonts.label, kFaint, L"CHASSIS", r, DT_LEFT | DT_VCENTER, Tracking());
    r = { rc.module.left, S(124), rc.module.right, S(140) };
    DrawLabel(dc, g_fonts.label, kFaint, L"MODULE", r, DT_LEFT | DT_VCENTER, Tracking());
    PaintFieldInfo(dc, d, rc);

    r = { pad, S(216), w - pad, S(232) };
    DrawLabel(dc, g_fonts.label, kFaint, L"VEHICLE / OPTIONAL", r, DT_LEFT | DT_VCENTER, Tracking());
    Fill(dc, rc.vehicleBox, kTile1);
    Frame(dc, rc.vehicleBox, GetFocus() == d->vehicle ? kAccent : kBorder);

    DashedFrame(dc, rc.note, kBorder);
    RECT nt = { rc.note.left + S(14), rc.note.top + S(10), rc.note.right - S(14), rc.note.bottom - S(8) };
    DrawLabel(dc, g_fonts.body, kSecondary,
              d->datenDir.empty()
                  ? L"Without SP-Daten nothing is suggested. Enter the chassis and module you picked in NCS Expert; "
                    L"NCS Dummy reads them from the copy's name."
                  : L"The module is a suggestion: SP-Daten function names can't prove which ECU wrote the trace. "
                    L"Check it against what you picked in NCS Expert.",
              nt, DT_LEFT | DT_TOP | DT_WORDBREAK);

    r = { pad, S(356), w - pad, S(372) };
    DrawLabel(dc, g_fonts.label, kFaint, L"COPY FOR NCS DUMMY", r, DT_LEFT | DT_VCENTER, Tracking());
    r = { pad, S(376), w - pad, S(398) };
    WString c, m;
    if (ReadCodes(d, &c, &m)) {
        WString name = c + L"-" + m + L"-" + d->stamp + kBackupTail;
        DrawLabel(dc, g_fonts.mono, kWhite, name.c_str(), r, DT_LEFT | DT_VCENTER);
    } else {
        DrawLabel(dc, g_fonts.label, kWarn, L"ENTER A CHASSIS AND A MODULE", r, DT_LEFT | DT_VCENTER, Tracking());
    }
    if (!d->error.empty()) {
        r = { pad, S(408), w - pad, rc.cancel.top - S(20) };
        DrawLabel(dc, g_fonts.body, kBad, d->error.c_str(), r, DT_LEFT | DT_TOP | DT_WORDBREAK);
    }
    rule = { pad, rc.cancel.top - S(16), w - pad, rc.cancel.top - S(15) };
    Fill(dc, rule, kRule);
}

static void PaintDialog(HDC dc, const RECT& client)
{
    BackupDialog* d = g_painting;
    int pad = S(24), w = client.right, h = client.bottom;
    Fill(dc, client, kPage);
    if (d->ecuMode) {
        PaintEcuDialog(dc, d, w);
        return;
    }

    RECT r = { pad, S(24), w - pad, S(40) };
    DrawLabel(dc, g_fonts.label, kFaint, L"01 / BACK UP", r, DT_LEFT | DT_VCENTER, Tracking());
    r = { pad, S(44), w - pad, S(74) };
    DrawLabel(dc, g_fonts.heading, kWhite, L"Back up FSW_PSW.TRC", r, DT_LEFT | DT_VCENTER);
    r = { pad, S(78), w - pad, S(96) };
    WString from = L"FROM " + d->source;
    DrawLabel(dc, g_fonts.label, kMuted, from.c_str(), r, DT_LEFT | DT_VCENTER | DT_PATH_ELLIPSIS, Tracking());
    RECT rule = { pad, S(110), w - pad, S(111) };
    Fill(dc, rule, kRule);

    Rects rc = DialogRects(w, h);
    r = { rc.chassis.left, S(124), rc.chassis.right, S(140) };
    DrawLabel(dc, g_fonts.label, kFaint, L"CHASSIS", r, DT_LEFT | DT_VCENTER, Tracking());
    r = { rc.module.left, S(124), rc.module.right, S(140) };
    DrawLabel(dc, g_fonts.label, kFaint, L"MODULE", r, DT_LEFT | DT_VCENTER, Tracking());
    PaintFieldInfo(dc, d, rc);

    r = { pad, S(218), rc.change.left - S(12), S(234) };
    DrawLabel(dc, g_fonts.label, kFaint, L"BACKUP FOLDER", r, DT_LEFT | DT_VCENTER, Tracking());
    r = { pad, S(236), rc.change.left - S(12), S(258) };
    DrawLabel(dc, g_fonts.body, kWhite, d->backupDir->c_str(), r, DT_LEFT | DT_VCENTER | DT_PATH_ELLIPSIS);

    r = { pad, S(274), w - pad, S(290) };
    DrawLabel(dc, g_fonts.label, kFaint, L"FILE NAME", r, DT_LEFT | DT_VCENTER, Tracking());
    r = { pad, S(292), w - pad, S(314) };
    WString c, m;
    if (ReadCodes(d, &c, &m)) {
        SYSTEMTIME now;
        GetLocalTime(&now);
        WString name = BackupStem(c, m, now) + kBackupTail;
        DrawLabel(dc, g_fonts.mono, kWhite, name.c_str(), r, DT_LEFT | DT_VCENTER);
    } else {
        DrawLabel(dc, g_fonts.label, kWarn, L"ENTER A CHASSIS AND A MODULE", r, DT_LEFT | DT_VCENTER, Tracking());
    }

    if (!d->error.empty()) {
        r = { pad, S(326), w - pad, rc.cancel.top - S(20) };
        DrawLabel(dc, g_fonts.body, kBad, d->error.c_str(), r, DT_LEFT | DT_TOP | DT_WORDBREAK);
    }
    rule = { pad, rc.cancel.top - S(16), w - pad, rc.cancel.top - S(15) };
    Fill(dc, rule, kRule);
}

static void SetError(BackupDialog* d, const WString& text)
{
    d->error = text;
    InvalidateRect(d->hwnd, NULL, FALSE);
}

static void RememberChoice(BackupDialog* d, const WString& module)
{
    // Remember the chassis as picked (E91), not its folder.
    WriteSetting(L"Chassis", SanitizeCode(GetWindowTextString(d->chassis), false));
    WriteSetting(L"Module", module);
    RememberModule(module);
}

static void DoConfirmEcu(BackupDialog* d)
{
    WString chassis, module;
    if (!ReadCodes(d, &chassis, &module)) {
        SetError(d, L"Enter a chassis and a module first.");
        SetFocus(GetWindowTextLengthW(d->chassis) ? d->module : d->chassis);
        return;
    }
    WString vehicle = GetWindowTextString(d->vehicle);
    size_t b = vehicle.find_first_not_of(L" \t");
    vehicle = b == WString::npos ? WString() : vehicle.substr(b, vehicle.find_last_not_of(L" \t") - b + 1);
    d->choice->chassis = SanitizeCode(GetWindowTextString(d->chassis), false);
    d->choice->daten = chassis;
    d->choice->module = module;
    d->choice->vehicle = vehicle;
    RememberChoice(d, module);
    d->succeeded = true;
    d->done = true;
}

static void DoBackup(BackupDialog* d)
{
    if (d->ecuMode) {
        DoConfirmEcu(d);
        return;
    }
    WString chassis, module;
    if (!ReadCodes(d, &chassis, &module)) {
        SetError(d, L"Enter a chassis and a module first.");
        SetFocus(GetWindowTextLengthW(d->chassis) ? d->module : d->chassis);
        return;
    }
    if (!FileExists(d->source)) {
        SetError(d, L"FSW_PSW.TRC isn't in the WORK folder any more. Read the car in NCS Expert again.");
        return;
    }
    DWORD err = EnsureDir(*d->backupDir);
    if (err) {
        SetError(d, L"Couldn't create the backup folder. " + SystemErrorText(err));
        return;
    }
    SYSTEMTIME now;
    GetLocalTime(&now);
    err = CopyToUniqueName(d->source, *d->backupDir, BackupStem(chassis, module, now), kBackupTail, &d->created);
    if (err == ERROR_FILE_EXISTS) {
        SetError(d, L"Too many backups with this name in one minute. Try again shortly.");
        return;
    }
    if (err) {
        SetError(d, L"Backup failed. " + SystemErrorText(err));
        return;
    }
    RememberChoice(d, module);
    d->succeeded = true;
    d->done = true;
}

static HWND CreateCombo(BackupDialog* d, int id, const RECT& r)
{
    HWND h = CreateWindowExW(0, L"COMBOBOX", L"",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWN |
                                 CBS_AUTOHSCROLL | CBS_UPPERCASE,
                             r.left, r.top, r.right - r.left, r.bottom - r.top, d->hwnd,
                             (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)g_fonts.mono, FALSE);
    SendMessageW(h, CB_LIMITTEXT, kMaxCodeLength, 0);
    SendMessageW(h, CB_SETITEMHEIGHT, (WPARAM)-1, S(30));  // the edit field
    SendMessageW(h, CB_SETITEMHEIGHT, 0, S(24));           // list rows
    ApplyThemedControl(h, L"DarkMode_CFD");
    COMBOBOXINFO info = {};
    info.cbSize = sizeof(info);
    if (GetComboBoxInfo(h, &info))
        ApplyThemedControl(info.hwndList, L"DarkMode_Explorer");
    return h;
}

static void CreateDialogControls(BackupDialog* d)
{
    RECT client;
    GetClientRect(d->hwnd, &client);
    Rects r = DialogRects(client.right, client.bottom);

    d->chassis = CreateCombo(d, IDC_CHASSIS, r.chassis);
    if (d->chassisList.empty()) {
        for (const wchar_t* c : kChassis)
            AddItem(d->chassis, c);
    } else {
        for (size_t i = 0; i < d->chassisList.size(); ++i)
            AddItem(d->chassis, d->chassisList[i].first);
    }
    SetWindowTextW(d->chassis, SanitizeCode(ReadSetting(L"Chassis"), false).c_str());

    d->module = CreateCombo(d, IDC_MODULE, r.module);
    SetWindowTextW(d->module, SanitizeCode(ReadSetting(L"Module"), true).c_str());
    UpdateForChassis(d);  // fills the module list, detects the module

    if (d->ecuMode) {
        d->vehicle = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                     r.vehicle.left, r.vehicle.top, r.vehicle.right - r.vehicle.left,
                                     r.vehicle.bottom - r.vehicle.top, d->hwnd, (HMENU)(INT_PTR)IDC_VEHICLE,
                                     GetModuleHandleW(NULL), NULL);
        SendMessageW(d->vehicle, WM_SETFONT, (WPARAM)g_fonts.mono, FALSE);
        SendMessageW(d->vehicle, EM_LIMITTEXT, kMaxVehicleLength, 0);
        SetWindowTextW(d->vehicle, d->choice->vehicle.c_str());
        d->cancel = CreateThemedButton(d->hwnd, IDC_READ_AGAIN, L"READ AGAIN", BUTTON_SECONDARY, kPage);
        d->ok = CreateThemedButton(d->hwnd, IDC_DO_BACKUP, L"CONFIRM", BUTTON_PRIMARY, kPage);
    } else {
        d->changeFolder = CreateThemedButton(d->hwnd, IDC_CHANGE_FOLDER, L"CHANGE", BUTTON_LINK, kPage);
        d->cancel = CreateThemedButton(d->hwnd, IDCANCEL, L"CANCEL", BUTTON_SECONDARY, kPage);
        d->ok = CreateThemedButton(d->hwnd, IDC_DO_BACKUP, L"BACK UP", BUTTON_PRIMARY, kPage);
        MoveWindow(d->changeFolder, r.change.left, r.change.top, r.change.right - r.change.left,
                   r.change.bottom - r.change.top, FALSE);
    }
    MoveWindow(d->cancel, r.cancel.left, r.cancel.top, r.cancel.right - r.cancel.left,
               r.cancel.bottom - r.cancel.top, FALSE);
    MoveWindow(d->ok, r.ok.left, r.ok.top, r.ok.right - r.ok.left, r.ok.bottom - r.ok.top, FALSE);
}

static LRESULT CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    BackupDialog* d = (BackupDialog*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_NCCREATE:
        d = (BackupDialog*)((CREATESTRUCTW*)lParam)->lpCreateParams;
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

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        SetTextColor((HDC)wParam, kWhite);
        SetBkColor((HDC)wParam, kTile1);
        return (LRESULT)g_fieldBrush;

    case WM_APP_REFRESH:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_APP_CHASSIS:
        UpdateForChassis(d);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_CHASSIS:
        case IDC_MODULE: {
            bool chassis = LOWORD(wParam) == IDC_CHASSIS;
            if (HIWORD(wParam) == CBN_EDITCHANGE) {
                d->error.clear();
                // Only once the typed chassis is a known one: halfway ("E9") would rescan and redetect.
                if (chassis && !FolderForChassis(d, SanitizeCode(GetWindowTextString(d->chassis), false)).empty())
                    UpdateForChassis(d);
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (HIWORD(wParam) == CBN_KILLFOCUS && chassis) {
                UpdateForChassis(d);  // catches a typed chassis SP-Daten doesn't have
            } else if (HIWORD(wParam) == CBN_SELCHANGE) {
                // The edit text updates after this notification.
                d->error.clear();
                PostMessageW(hwnd, chassis ? WM_APP_CHASSIS : WM_APP_REFRESH, 0, 0);
            }
            return 0;
        }
        case IDC_CHANGE_FOLDER: {
            WString dir = PickFolder(hwnd, L"Choose the folder where TRC backups are saved.", *d->backupDir);
            if (!dir.empty()) {
                *d->backupDir = dir;
                WriteSetting(L"BackupDir", dir);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        case IDC_DO_BACKUP:
            DoBackup(d);
            return 0;
        case IDC_READ_AGAIN:
            d->readAgain = true;
            d->done = true;
            return 0;
        case IDC_VEHICLE:
            if (HIWORD(wParam) == EN_SETFOCUS || HIWORD(wParam) == EN_KILLFOCUS)
                InvalidateRect(hwnd, NULL, FALSE);  // the field's frame shows focus
            return 0;
        case IDOK: {  // Enter: press the focused button, else back up
            HWND focus = GetFocus();
            if (focus == d->cancel || focus == d->changeFolder)
                SendMessageW(focus, BM_CLICK, 0, 0);
            else
                DoBackup(d);
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

static void LoadTraceAndDaten(BackupDialog* d, const WString& trace, const WString& workDir)
{
    d->datenDir = FindDatenDir(NormalizeDir(workDir));
    d->chassisList = LoadChassisList(d->datenDir);
    String data;
    d->traceRead = ReadFileBytes(trace, &data);
    if (d->traceRead)
        d->trace = ParseTrace(data.data(), data.size());
}

bool RunBackupDialog(HWND owner, const WString& source, WString& backupDir, WString* created)
{
    BackupDialog d = {};
    d.source = source;
    d.backupDir = &backupDir;
    LoadTraceAndDaten(&d, source, source.substr(0, source.size() - FileNameOf(source).size()));

    HWND hwnd = CreateModal(owner, kDialogClass, DialogProc, L"Back up TRC", S(520), S(460), &d);
    if (!hwnd)
        return false;
    SendMessageW(d.chassis, CB_SETEDITSEL, 0, MAKELPARAM(0, -1));
    RunModal(owner, hwnd, d.chassis, &d.done);
    if (d.succeeded)
        *created = d.created;
    return d.succeeded;
}

EcuResult RunEcuDialog(HWND owner, const WString& trace, const WString& workDir, const WString& label,
                       const WString& info, const WString& stamp, EcuChoice* choice)
{
    BackupDialog d = {};
    d.ecuMode = true;
    d.source = trace;
    d.label = label;
    d.info = info;
    d.stamp = stamp;
    d.choice = choice;
    LoadTraceAndDaten(&d, trace, workDir);

    HWND hwnd = CreateModal(owner, kDialogClass, DialogProc, L"Trace captured", S(520), S(540), &d);
    if (!hwnd)
        return ECU_CLOSED;
    SendMessageW(d.chassis, CB_SETEDITSEL, 0, MAKELPARAM(0, -1));
    RunModal(owner, hwnd, d.chassis, &d.done);
    return d.succeeded ? ECU_CONFIRMED : d.readAgain ? ECU_READ_AGAIN : ECU_CLOSED;
}
