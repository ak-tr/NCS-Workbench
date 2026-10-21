// Paths, settings and formatting helpers. See util.h.

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>

#include <vector>

#include "util.h"

static const wchar_t kSettingsKey[] = L"Software\\NCS Workbench";  // under HKEY_CURRENT_USER
static const wchar_t* const kMonths[] = { L"JAN", L"FEB", L"MAR", L"APR", L"MAY", L"JUN",
                                          L"JUL", L"AUG", L"SEP", L"OCT", L"NOV", L"DEC" };

static HKEY g_settings;
static wchar_t g_thousandSep[8] = L",";

WString GetWindowTextString(HWND hwnd)
{
    int len = GetWindowTextLengthW(hwnd);
    std::vector<wchar_t> buf(len + 1);
    GetWindowTextW(hwnd, &buf[0], len + 1);
    return &buf[0];
}

void Place(HWND hwnd, const RECT& r)
{
    MoveWindow(hwnd, r.left, r.top, r.right - r.left, r.bottom - r.top, TRUE);
}

void FitListColumn(HWND list)
{
    RECT rc;
    GetClientRect(list, &rc);
    if (ListView_GetColumnWidth(list, 0) != rc.right)
        ListView_SetColumnWidth(list, 0, rc.right);
}

WString Widen(const String& s)
{
    WString out;
    for (size_t i = 0; i < s.size(); ++i)
        out += (wchar_t)(unsigned char)s[i];
    return out;
}

// ---------------------------------------------------------------------------
// Paths

WString NormalizeDir(WString p)
{
    const wchar_t* junk = L" \t\"";
    size_t b = p.find_first_not_of(junk);
    if (b == WString::npos)
        return WString();
    p = p.substr(b, p.find_last_not_of(junk) - b + 1);
    if (p.size() == 2 && p[1] == L':')
        p += L'\\';
    while (p.size() > 1 && (p[p.size() - 1] == L'\\' || p[p.size() - 1] == L'/') &&
           !(p.size() == 3 && p[1] == L':'))
        p.erase(p.size() - 1);
    return p;
}

WString JoinPath(const WString& dir, const WString& name)
{
    if (!dir.empty() && dir[dir.size() - 1] == L'\\')
        return dir + name;
    return dir + L'\\' + name;
}

WString FileNameOf(const WString& path)
{
    size_t slash = path.find_last_of(L"\\/");
    return slash == WString::npos ? path : path.substr(slash + 1);
}

bool DirExists(const WString& dir)
{
    DWORD attr = GetFileAttributesW(dir.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool FileExists(const WString& path)
{
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

DWORD EnsureDir(const WString& dir)
{
    if (DirExists(dir) || CreateDirectoryW(dir.c_str(), NULL))
        return 0;
    return GetLastError();
}

DWORD CopyToUniqueName(const WString& source, const WString& dir, const WString& stem, const WString& tail,
                       WString* created)
{
    for (int attempt = 1; attempt <= 99; ++attempt) {
        WString name = stem;
        if (attempt > 1) {
            wchar_t suffix[8];
            wsprintfW(suffix, L"-%d", attempt);
            name += suffix;
        }
        WString target = JoinPath(dir, name + tail);
        if (CopyFileW(source.c_str(), target.c_str(), TRUE)) {  // TRUE: never overwrite
            *created = target;
            return 0;
        }
        DWORD err = GetLastError();
        if (err != ERROR_FILE_EXISTS && err != ERROR_ALREADY_EXISTS)
            return err;
    }
    return ERROR_FILE_EXISTS;
}

bool ReadFileBytes(const WString& path, String* out)
{
    // Share write/delete too: NCS Expert may have the file open.
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    DWORD size = GetFileSize(h, NULL);
    bool ok = size != INVALID_FILE_SIZE && size <= 8 * 1024 * 1024;
    if (ok) {
        out->resize(size);
        DWORD read = 0;
        ok = size == 0 || (ReadFile(h, &(*out)[0], size, &read, NULL) && read == size);
    }
    CloseHandle(h);
    return ok;
}

// ---------------------------------------------------------------------------
// Settings

void InitSettings()
{
    GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_STHOUSAND, g_thousandSep, 8);
    // Opened, not created: launching alone leaves nothing behind.
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, KEY_READ | KEY_WRITE, &g_settings) != ERROR_SUCCESS)
        g_settings = NULL;
}

void CloseSettings()
{
    if (g_settings)
        RegCloseKey(g_settings);
    g_settings = NULL;
}

WString ReadSetting(const wchar_t* key, const wchar_t* def)
{
    wchar_t buf[1024];
    DWORD type = 0, bytes = sizeof(buf) - sizeof(wchar_t);
    if (!g_settings ||
        RegQueryValueExW(g_settings, key, NULL, &type, (BYTE*)buf, &bytes) != ERROR_SUCCESS || type != REG_SZ)
        return def;
    buf[bytes / sizeof(wchar_t)] = 0;  // registry strings aren't always terminated
    return buf;
}

void WriteSetting(const wchar_t* key, const WString& value)
{
    if (!g_settings &&
        RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, NULL, 0, KEY_READ | KEY_WRITE, NULL, &g_settings,
                        NULL) != ERROR_SUCCESS) {
        g_settings = NULL;
        return;
    }
    RegSetValueExW(g_settings, key, 0, REG_SZ, (const BYTE*)value.c_str(),
                   (DWORD)((value.size() + 1) * sizeof(wchar_t)));
}

// ---------------------------------------------------------------------------
// Shell

static int CALLBACK BrowseCallback(HWND hwnd, UINT msg, LPARAM, LPARAM data)
{
    if (msg == BFFM_INITIALIZED && data)
        SendMessageW(hwnd, BFFM_SETSELECTIONW, TRUE, data);
    return 0;
}

WString PickFolder(HWND owner, const wchar_t* title, const WString& initial)
{
    BROWSEINFOW bi = {};
    bi.hwndOwner = owner;
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;
    bi.lpfn = BrowseCallback;
    bi.lParam = (LPARAM)initial.c_str();
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl)
        return WString();
    wchar_t buf[MAX_PATH];
    WString result;
    if (SHGetPathFromIDListW(pidl, buf))
        result = NormalizeDir(buf);
    CoTaskMemFree(pidl);
    return result;
}

void ShowInExplorer(const WString& path)
{
    if (FileExists(path)) {
        WString args = L"/select,\"" + path + L"\"";
        ShellExecuteW(NULL, L"open", L"explorer.exe", args.c_str(), NULL, SW_SHOWNORMAL);
    } else {
        WString dir = path.substr(0, path.size() - FileNameOf(path).size());
        ShellExecuteW(NULL, L"open", NormalizeDir(dir).c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
}

WString SystemErrorText(DWORD code)
{
    wchar_t* buf = NULL;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                 FORMAT_MESSAGE_IGNORE_INSERTS,
                             NULL, code, 0, (LPWSTR)&buf, 0, NULL);
    WString text;
    if (n && buf)
        text.assign(buf, n);
    LocalFree(buf);
    while (!text.empty() && (text[text.size() - 1] == L'\n' || text[text.size() - 1] == L'\r' ||
                             text[text.size() - 1] == L' '))
        text.erase(text.size() - 1);
    if (text.empty()) {
        wchar_t num[32];
        wsprintfW(num, L"Windows error %lu.", code);
        text = num;
    }
    return text;
}

// ---------------------------------------------------------------------------
// Formatting

WString FormatThousands(ULONGLONG n)
{
    WString digits;
    do {
        digits.insert(digits.begin(), wchar_t(L'0' + n % 10));
        n /= 10;
    } while (n);
    WString out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i && (digits.size() - i) % 3 == 0)
            out += g_thousandSep;
        out += digits[i];
    }
    return out;
}

WString FormatShortSize(ULONGLONG n)
{
    wchar_t buf[32];
    if (n < 1024)
        wsprintfW(buf, L"%u B", (unsigned)n);
    else if (n < 1024 * 1024)
        wsprintfW(buf, L"%u KB", (unsigned)((n + 512) / 1024));
    else
        wsprintfW(buf, L"%u.%u MB", (unsigned)(n / (1024 * 1024)),
                  (unsigned)(n % (1024 * 1024) * 10 / (1024 * 1024)));
    return buf;
}

static bool ToLocal(const FILETIME& ft, SYSTEMTIME* local)
{
    SYSTEMTIME utc;
    return FileTimeToSystemTime(&ft, &utc) && SystemTimeToTzSpecificLocalTime(NULL, &utc, local);
}

WString FormatStamp(const FILETIME& ft)
{
    SYSTEMTIME t, now;
    if (!ToLocal(ft, &t))
        return WString();
    GetLocalTime(&now);
    wchar_t buf[32];
    if (t.wYear == now.wYear)
        wsprintfW(buf, L"%u %s %02u:%02u", t.wDay, kMonths[t.wMonth - 1], t.wHour, t.wMinute);
    else
        wsprintfW(buf, L"%u %s %u", t.wDay, kMonths[t.wMonth - 1], t.wYear);
    return buf;
}

WString FormatShortStamp(const FILETIME& ft)
{
    SYSTEMTIME t, now;
    if (!ToLocal(ft, &t))
        return WString();
    GetLocalTime(&now);
    wchar_t buf[16];
    if (t.wYear == now.wYear && t.wMonth == now.wMonth && t.wDay == now.wDay)
        wsprintfW(buf, L"%02u:%02u", t.wHour, t.wMinute);
    else
        wsprintfW(buf, L"%u %s", t.wDay, kMonths[t.wMonth - 1]);
    return buf;
}

WString FormatFileStamp(const SYSTEMTIME& t)
{
    wchar_t buf[32];
    wsprintfW(buf, L"%04u%02u%02u-%02u%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute);
    return buf;
}

void CopyToClipboard(HWND owner, const WString& text)
{
    if (!OpenClipboard(owner))
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
