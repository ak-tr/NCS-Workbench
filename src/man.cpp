// Empty MAN dialog. See man.h.

#include <windows.h>

#include "man.h"
#include "modal.h"
#include "theme.h"
#include "util.h"

static const wchar_t kDialogClass[] = L"NcsWorkbenchEmptyMan";
static const wchar_t kBackupTail[] = L"-FSW_PSW.MAN";

enum { IDC_CHANGE_FOLDER = 100, IDC_EMPTY };

struct EmptyManDialog {
    HWND hwnd;
    HWND changeFolder, cancel, ok;
    WString man;
    WString* backupDir;
    WString backup;
    WString error;
    bool done;
    bool succeeded;
};

static EmptyManDialog* g_painting;  // PaintBuffered takes a plain function

static bool ReadFileInfo(const WString& path, ULONGLONG* size, FILETIME* modified)
{
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a) ||
        (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        return false;
    *size = ((ULONGLONG)a.nFileSizeHigh << 32) | a.nFileSizeLow;
    *modified = a.ftLastWriteTime;
    return true;
}

static void DialogRects(int w, int h, RECT* change, RECT* cancel, RECT* ok)
{
    int pad = S(24);
    *change = { w - pad - S(80), S(200), w - pad, S(224) };
    int by = h - pad - S(44);
    *ok = { w - pad - S(150), by, w - pad, by + S(44) };
    *cancel = { ok->left - S(8) - S(120), by, ok->left - S(8), by + S(44) };
}

static void PaintDialog(HDC dc, const RECT& client)
{
    EmptyManDialog* d = g_painting;
    int pad = S(24), w = client.right, h = client.bottom;
    Fill(dc, client, kPage);

    RECT r = { pad, S(24), w - pad, S(40) };
    DrawLabel(dc, g_fonts.label, kFaint, L"02 / EMPTY MAN", r, DT_LEFT | DT_VCENTER, Tracking());
    r = { pad, S(44), w - pad, S(74) };
    DrawLabel(dc, g_fonts.heading, kWhite, L"Empty FSW_PSW.MAN?", r, DT_LEFT | DT_VCENTER);
    r = { pad, S(82), w - pad, S(124) };
    DrawLabel(dc, g_fonts.body, kSecondary,
              L"This clears your edits so you can start a fresh one in NCS Dummy. "
              L"The current file is backed up first.",
              r, DT_LEFT | DT_TOP | DT_WORDBREAK);
    RECT rule = { pad, S(134), w - pad, S(135) };
    Fill(dc, rule, kRule);

    RECT change, cancel, ok;
    DialogRects(w, h, &change, &cancel, &ok);
    r = { pad, S(148), w - pad, S(164) };
    DrawLabel(dc, g_fonts.label, kFaint, L"CURRENT FILE", r, DT_LEFT | DT_VCENTER, Tracking());
    r = { pad, S(166), w - pad, S(188) };
    ULONGLONG size;
    FILETIME modified;
    if (ReadFileInfo(d->man, &size, &modified)) {
        WString info = L"FSW_PSW.MAN / " + FormatThousands(size) + L" B / " +
                       (IsRecent(modified) ? FormatAge(modified) : FormatStamp(modified));
        DrawLabel(dc, g_fonts.mono, kWhite, info.c_str(), r, DT_LEFT | DT_VCENTER);
    } else {
        DrawLabel(dc, g_fonts.label, kWarn, L"FSW_PSW.MAN NOT FOUND", r, DT_LEFT | DT_VCENTER, Tracking());
    }

    r = { pad, S(204), change.left - S(12), S(220) };
    DrawLabel(dc, g_fonts.label, kFaint, L"BACKED UP FIRST AS", r, DT_LEFT | DT_VCENTER, Tracking());
    SYSTEMTIME now;
    GetLocalTime(&now);
    WString name = FormatFileStamp(now) + kBackupTail;
    r = { pad, S(226), w - pad, S(248) };
    DrawLabel(dc, g_fonts.mono, kWhite, name.c_str(), r, DT_LEFT | DT_VCENTER);
    r = { pad, S(250), w - pad, S(266) };
    WString in = L"IN " + *d->backupDir;
    DrawLabel(dc, g_fonts.label, kMuted, in.c_str(), r, DT_LEFT | DT_VCENTER | DT_PATH_ELLIPSIS, Tracking());

    if (!d->error.empty()) {
        r = { pad, S(280), w - pad, cancel.top - S(20) };
        DrawLabel(dc, g_fonts.body, kBad, d->error.c_str(), r, DT_LEFT | DT_TOP | DT_WORDBREAK);
    }
    rule = { pad, cancel.top - S(16), w - pad, cancel.top - S(15) };
    Fill(dc, rule, kRule);
}

static void SetError(EmptyManDialog* d, const WString& text)
{
    d->error = text;
    InvalidateRect(d->hwnd, NULL, FALSE);
}

static void DoEmpty(EmptyManDialog* d)
{
    ULONGLONG size;
    FILETIME modified;
    if (!ReadFileInfo(d->man, &size, &modified)) {
        SetError(d, L"FSW_PSW.MAN isn't in the WORK folder any more.");
        return;
    }
    if (size == 0) {
        SetError(d, L"FSW_PSW.MAN is already empty.");
        return;
    }
    DWORD err = EnsureDir(*d->backupDir);
    if (err) {
        SetError(d, L"Couldn't create the backup folder, so the MAN wasn't emptied. " + SystemErrorText(err));
        return;
    }
    SYSTEMTIME now;
    GetLocalTime(&now);
    err = CopyToUniqueName(d->man, *d->backupDir, FormatFileStamp(now), kBackupTail, &d->backup);
    if (err) {
        SetError(d, L"Couldn't back up the MAN, so it wasn't emptied. " +
                        (err == ERROR_FILE_EXISTS ? WString(L"Too many backups this minute.") : SystemErrorText(err)));
        return;
    }

    // Only after the backup exists: truncate in place (keeps the file and its attributes).
    HANDLE h = CreateFileW(d->man.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, TRUNCATE_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        err = GetLastError();
        SetError(d, L"The MAN was backed up as " + FileNameOf(d->backup) +
                        L" but couldn't be emptied. Close NCS Expert and try again. " + SystemErrorText(err));
        return;
    }
    CloseHandle(h);
    d->succeeded = true;
    d->done = true;
}

static void CreateDialogControls(EmptyManDialog* d)
{
    RECT client;
    GetClientRect(d->hwnd, &client);
    RECT change, cancel, ok;
    DialogRects(client.right, client.bottom, &change, &cancel, &ok);
    d->changeFolder = CreateThemedButton(d->hwnd, IDC_CHANGE_FOLDER, L"CHANGE", BUTTON_LINK, kPage);
    d->cancel = CreateThemedButton(d->hwnd, IDCANCEL, L"CANCEL", BUTTON_SECONDARY, kPage);
    d->ok = CreateThemedButton(d->hwnd, IDC_EMPTY, L"EMPTY MAN", BUTTON_DANGER, kPage);
    MoveWindow(d->changeFolder, change.left, change.top, change.right - change.left, change.bottom - change.top, FALSE);
    MoveWindow(d->cancel, cancel.left, cancel.top, cancel.right - cancel.left, cancel.bottom - cancel.top, FALSE);
    MoveWindow(d->ok, ok.left, ok.top, ok.right - ok.left, ok.bottom - ok.top, FALSE);
}

static LRESULT CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    EmptyManDialog* d = (EmptyManDialog*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_NCCREATE:
        d = (EmptyManDialog*)((CREATESTRUCTW*)lParam)->lpCreateParams;
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

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_CHANGE_FOLDER: {
            WString dir = PickFolder(hwnd, L"Choose the folder where backups are saved.", *d->backupDir);
            if (!dir.empty()) {
                *d->backupDir = dir;
                WriteSetting(L"BackupDir", dir);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        case IDC_EMPTY:
            DoEmpty(d);
            return 0;
        case IDOK: {  // Enter presses the focused button (Cancel by default)
            HWND focus = GetFocus();
            if (focus == d->ok || focus == d->changeFolder || focus == d->cancel)
                SendMessageW(focus, BM_CLICK, 0, 0);
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

bool RunEmptyManDialog(HWND owner, const WString& manPath, WString& backupDir, WString* backup)
{
    EmptyManDialog d = {};
    d.man = manPath;
    d.backupDir = &backupDir;
    HWND hwnd = CreateModal(owner, kDialogClass, DialogProc, L"Empty MAN", S(520), S(400), &d);
    if (!hwnd)
        return false;
    // Destructive: Cancel has focus, so a stray Enter doesn't empty the file.
    RunModal(owner, hwnd, d.cancel, &d.done);
    if (d.succeeded)
        *backup = d.backup;
    return d.succeeded;
}
