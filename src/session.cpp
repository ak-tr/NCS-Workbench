// Guided coding session. See session.h.

#include <windows.h>

#include <algorithm>
#include <map>
#include <vector>

#include "session.h"
#include "sha256.h"
#include "util.h"

static const wchar_t kTrcName[] = L"FSW_PSW.TRC";
static const wchar_t kManName[] = L"FSW_PSW.MAN";
static const wchar_t kIniName[] = L"session.ini";
static const wchar_t kIniTemp[] = L"session.ini.tmp";
static const wchar_t* const kMonths[] = { L"JAN", L"FEB", L"MAR", L"APR", L"MAY", L"JUN",
                                          L"JUL", L"AUG", L"SEP", L"OCT", L"NOV", L"DEC" };

// Saved names, indexed by the enums.
static const wchar_t* const kStateNames[] = {
    L"NONE",       L"PREPARING", L"AWAITING_READ",     L"AWAITING_ECU", L"AWAITING_MAN",
    L"REVIEWING",  L"CODING",    L"CODED",             L"AWAITING_READBACK", L"VERIFYING",
    L"COMPLETED",  L"CANCELLED", L"FAILED",
};
static const wchar_t* const kKindNames[] = { L"before-trc", L"before-man", L"read",     L"dummy",
                                             L"man",        L"work-trc",   L"work-man", L"readback" };
static const wchar_t* const kOutcomeNames[] = { L"", L"verified", L"discrepancies", L"unverified", L"cancelled",
                                                L"failed", L"read-back" };
static const wchar_t* const kTypeNames[] = { L"MAN", L"VO" };

// ---------------------------------------------------------------------------
// Basics

void InitSession(Session* s)
{
    *s = Session();
}

bool IsSessionOpen(SessionState state)
{
    return state != SESSION_NONE && state != SESSION_COMPLETED && state != SESSION_CANCELLED &&
           state != SESSION_FAILED;
}

int SessionStage(SessionState state)
{
    switch (state) {
    case SESSION_NONE:
        return 0;
    case SESSION_PREPARING:
        return 1;
    case SESSION_AWAITING_READ:
    case SESSION_AWAITING_ECU:
        return 2;
    case SESSION_AWAITING_MAN:
        return 3;
    case SESSION_REVIEWING:
        return 4;
    case SESSION_CODING:
        return 5;
    case SESSION_CODED:
    case SESSION_AWAITING_READBACK:
    case SESSION_VERIFYING:
        return 6;
    default:
        return 7;
    }
}

int SessionStage(const Session& s)
{
    int stage = SessionStage(s.state);
    if (s.type != SESSION_TYPE_VO)
        return stage;
    // VO has no edit and review: 01 prepare, 02 read, 03 code, 04 read back, 05 changes.
    switch (s.state) {
    case SESSION_CODING:
        return 3;
    case SESSION_CODED:
    case SESSION_AWAITING_READBACK:
        return 4;
    default:
        return stage <= 2 ? stage : 5;
    }
}

int StageCount(SessionType type)
{
    return type == SESSION_TYPE_VO ? 5 : 7;
}

int LastTileStage(SessionType type)
{
    return type == SESSION_TYPE_VO ? 5 : 6;
}

const wchar_t* OutcomeText(Outcome outcome)
{
    switch (outcome) {
    case OUTCOME_VERIFIED:
        return L"VERIFIED";
    case OUTCOME_READ_BACK:
        return L"READ BACK";
    case OUTCOME_DISCREPANCIES:
        return L"DISCREPANCIES";
    case OUTCOME_UNVERIFIED:
        return L"NOT VERIFIED";
    case OUTCOME_CANCELLED:
        return L"CANCELLED";
    case OUTCOME_FAILED:
        return L"FAILED";
    default:
        return L"";
    }
}

bool HasTime(const SYSTEMTIME& t)
{
    return t.wYear != 0;
}

WString FormatClock(const SYSTEMTIME& t)
{
    wchar_t buf[16];
    wsprintfW(buf, L"%02u:%02u", t.wHour, t.wMinute);
    return buf;
}

WString FormatDay(const SYSTEMTIME& t)
{
    if (!HasTime(t))
        return WString();
    wchar_t buf[32];
    wsprintfW(buf, L"%u %s %02u:%02u", t.wDay, kMonths[(t.wMonth + 11) % 12], t.wHour, t.wMinute);
    return buf;
}

WString FormatDayAge(const SYSTEMTIME& t)
{
    if (!HasTime(t))
        return WString();
    FILETIME ft = LocalToFileTime(t);
    return IsRecent(ft) ? FormatAge(ft) : FormatDay(t);
}

static WString FormatIsoTime(const SYSTEMTIME& t)
{
    if (!HasTime(t))
        return WString();
    wchar_t buf[32];
    wsprintfW(buf, L"%04u-%02u-%02u %02u:%02u:%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    return buf;
}

static int ParseInt(const WString& s)
{
    int n = 0;
    for (size_t i = 0; i < s.size() && s[i] >= L'0' && s[i] <= L'9'; ++i)
        n = n * 10 + (s[i] - L'0');
    return n;
}

static SYSTEMTIME ParseIsoTime(const WString& s)
{
    SYSTEMTIME t = {};
    if (s.size() >= 19) {
        t.wYear = (WORD)ParseInt(s.substr(0, 4));
        t.wMonth = (WORD)ParseInt(s.substr(5, 2));
        t.wDay = (WORD)ParseInt(s.substr(8, 2));
        t.wHour = (WORD)ParseInt(s.substr(11, 2));
        t.wMinute = (WORD)ParseInt(s.substr(14, 2));
        t.wSecond = (WORD)ParseInt(s.substr(17, 2));
    }
    return t;
}

static WString IntText(int n)
{
    wchar_t buf[16];
    wsprintfW(buf, L"%d", n);
    return buf;
}

WString SessionsRoot(const WString& backupDir)
{
    return JoinPath(backupDir, L"Sessions");
}

WString SnapshotPath(const Session& s, const Snapshot& snap)
{
    return JoinPath(s.dir, snap.file);
}

const Snapshot* FindSnapshot(const Session& s, int id)
{
    for (size_t i = 0; i < s.snaps.size(); ++i) {
        if (s.snaps[i].id == id)
            return &s.snaps[i];
    }
    return NULL;
}

static const Snapshot* LastSnapshotFrom(const Session& s, SnapKind kind, int fromId)
{
    for (size_t i = s.snaps.size(); i-- > 0;) {
        if (s.snaps[i].kind == kind && s.snaps[i].id >= fromId)
            return &s.snaps[i];
    }
    return NULL;
}

const Snapshot* LastSnapshot(const Session& s, SnapKind kind)
{
    return LastSnapshotFrom(s, kind, 0);
}

int CountSnapshots(const Session& s, SnapKind kind)
{
    int n = 0;
    for (size_t i = 0; i < s.snaps.size(); ++i)
        n += s.snaps[i].kind == kind ? 1 : 0;
    return n;
}

static int NextSnapshotId(const Session& s)
{
    int id = 0;
    for (size_t i = 0; i < s.snaps.size(); ++i)
        id = std::max(id, s.snaps[i].id);
    return id + 1;
}

static WString WorkPath(const Session& s, const wchar_t* name)
{
    return JoinPath(s.workDir, name);
}

// ---------------------------------------------------------------------------
// Files

bool ProbeFile(const WString& path, ULONGLONG* size, FILETIME* modified)
{
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a) ||
        (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        return false;
    *size = ((ULONGLONG)a.nFileSizeHigh << 32) | a.nFileSizeLow;
    *modified = a.ftLastWriteTime;
    return true;
}

static bool IsMissing(DWORD err)
{
    return err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND;
}

static bool IsBusy(DWORD err)
{
    return err == ERROR_SHARING_VIOLATION || err == ERROR_LOCK_VIOLATION;
}

// Reads the whole file while nobody may write to it: fails with
// ERROR_SHARING_VIOLATION while NCS Expert or NCS Dummy still has it open
// for writing. Returns 0 or a Windows error.
static DWORD ReadLocked(const WString& path, String* out)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                           NULL);
    if (h == INVALID_HANDLE_VALUE)
        return GetLastError();
    DWORD err = 0;
    DWORD size = GetFileSize(h, NULL);
    if (size == INVALID_FILE_SIZE) {
        err = GetLastError();
    } else if (size > 8 * 1024 * 1024) {
        err = ERROR_FILE_TOO_LARGE;
    } else {
        out->resize(size);
        DWORD read = 0;
        if (size && (!ReadFile(h, &(*out)[0], size, &read, NULL) || read != size)) {
            err = GetLastError();
            if (!err)
                err = ERROR_READ_FAULT;
        }
    }
    CloseHandle(h);
    return err;
}

static DWORD WriteNewFile(const WString& path, const String& data)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return GetLastError();
    DWORD err = 0, written = 0;
    if (!data.empty() && (!WriteFile(h, data.data(), (DWORD)data.size(), &written, NULL) || written != data.size())) {
        err = GetLastError();
        if (!err)
            err = ERROR_WRITE_FAULT;
    }
    if (!err && !FlushFileBuffers(h))
        err = GetLastError();
    CloseHandle(h);
    if (err)
        DeleteFileW(path.c_str());
    return err;
}

static WString ReadError(const wchar_t* name, DWORD err)
{
    if (IsBusy(err))
        return WString(name) + L" is open in another program, probably NCS Expert or NCS Dummy. Close it there "
                               L"and retry.";
    return L"Couldn't read " + WString(name) + L". " + SystemErrorText(err);
}

static void DefaultSnapshotName(const Session& s, SnapKind kind, WString* stem, WString* tail)
{
    bool man = kind == SNAP_BEFORE_MAN || kind == SNAP_MAN || kind == SNAP_WORK_MAN;
    *tail = man ? L"-FSW_PSW.MAN" : L"-FSW_PSW.TRC";
    if (kind == SNAP_BEFORE_TRC || kind == SNAP_BEFORE_MAN) {
        *stem = L"before";
        return;
    }
    const wchar_t* prefix = kind == SNAP_READ       ? L"read"
                            : kind == SNAP_MAN      ? L"man"
                            : kind == SNAP_READBACK ? L"readback"
                                                    : L"work";
    *stem = WString(prefix) + L"-" + IntText(CountSnapshots(s, kind) + 1);
}

// Writes data as a new read-only file in the session folder (stem, then
// stem-2, ... + tail; never overwriting), checks it reads back identical,
// records it and saves the session.
static WString WriteSnapshot(Session* s, SnapKind kind, const String& data, const WString& source, const WString& stem,
                             const WString& tail)
{
    for (int attempt = 1; attempt <= 99; ++attempt) {
        WString name = stem;
        if (attempt > 1)
            name += L"-" + IntText(attempt);
        name += tail;
        WString path = JoinPath(s->dir, name);
        DWORD err = WriteNewFile(path, data);
        if (err == ERROR_FILE_EXISTS || err == ERROR_ALREADY_EXISTS)
            continue;
        if (err)
            return L"Couldn't save " + name + L" in the session folder. " + SystemErrorText(err);
        String check;
        if (!ReadFileBytes(path, &check) || check != data)
            return L"The copy " + name + L" in the session folder doesn't read back the same. Check the drive.";
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY);

        Snapshot snap = {};
        snap.id = NextSnapshotId(*s);
        snap.kind = kind;
        snap.file = name;
        snap.sha256 = Sha256Hex(data.data(), data.size());
        snap.size = data.size();
        GetLocalTime(&snap.taken);
        snap.source = source;
        s->snaps.push_back(snap);
        if (!SaveSession(s))
            return s->error;
        return WString();
    }
    return L"Too many files named " + stem + tail + L" in the session folder.";
}

// The snapshot file is still there with the content it was saved with.
static bool SnapshotIntact(const Session& s, const Snapshot& snap)
{
    String data;
    return ReadFileBytes(SnapshotPath(s, snap), &data) && Sha256Hex(data.data(), data.size()) == snap.sha256;
}

// Makes sure the WORK file's current content is saved in the session folder
// (as `kind`, unless an intact snapshot already holds the same bytes).
// *exists: whether the file is there. An empty file is only saved if saveEmpty.
static WString ArchiveWorkFile(Session* s, const wchar_t* name, SnapKind kind, bool saveEmpty, bool* exists,
                               String* sha, String* bytes)
{
    WString path = WorkPath(*s, name);
    String data;
    DWORD err = ReadLocked(path, &data);
    *exists = false;
    if (IsMissing(err))
        return WString();
    if (err)
        return ReadError(name, err);
    *exists = true;
    *sha = Sha256Hex(data.data(), data.size());
    if (bytes)
        *bytes = data;
    if (data.empty() && !saveEmpty)
        return WString();
    for (size_t i = 0; i < s->snaps.size(); ++i) {
        if (s->snaps[i].sha256 == *sha && SnapshotIntact(*s, s->snaps[i]))
            return WString();
    }
    WString stem, tail;
    DefaultSnapshotName(*s, kind, &stem, &tail);
    return WriteSnapshot(s, kind, data, path, stem, tail);
}

static WString ChangedMessage(const wchar_t* name, const wchar_t* action)
{
    return WString(name) + L" changed while Workbench was saving it, so it wasn't " + action +
           L". Retry to save the new version.";
}

// Deletes the WORK file if it still has the archived content.
static WString DeleteIfUnchanged(const WString& path, const String& sha, const wchar_t* name)
{
    String data;
    DWORD err = ReadLocked(path, &data);
    if (IsMissing(err))
        return WString();
    if (err)
        return ReadError(name, err);
    if (Sha256Hex(data.data(), data.size()) != sha)
        return ChangedMessage(name, L"deleted");
    if (!DeleteFileW(path.c_str()))
        return L"Couldn't delete " + WString(name) + L". " + SystemErrorText(GetLastError());
    return WString();
}

// Empties the WORK file if it still has the archived content (checked and
// truncated under one handle that keeps writers out), or creates it empty.
static WString EmptyIfUnchanged(const WString& path, bool existed, const String& sha, const wchar_t* name)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (!IsMissing(err))
            return IsBusy(err) ? ReadError(name, err) : L"Couldn't open " + WString(name) + L". " + SystemErrorText(err);
        if (existed)
            return ChangedMessage(name, L"emptied");  // it went away meanwhile; look again
        h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE)
            return L"Couldn't create an empty " + WString(name) + L". " + SystemErrorText(GetLastError());
        CloseHandle(h);
        return WString();
    }
    String data;
    DWORD size = GetFileSize(h, NULL);
    DWORD err = 0;
    if (size == INVALID_FILE_SIZE || size > 8 * 1024 * 1024) {
        err = size == INVALID_FILE_SIZE ? GetLastError() : ERROR_FILE_TOO_LARGE;
    } else if (size) {
        data.resize(size);
        DWORD read = 0;
        if (!ReadFile(h, &data[0], size, &read, NULL) || read != size)
            err = ERROR_READ_FAULT;
    }
    WString result;
    if (err) {
        result = L"Couldn't read " + WString(name) + L". " + SystemErrorText(err);
    } else if (!existed || Sha256Hex(data.data(), data.size()) != sha) {
        result = ChangedMessage(name, L"emptied");
    } else if (size && (SetFilePointer(h, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER || !SetEndOfFile(h) ||
                        !FlushFileBuffers(h))) {
        result = L"Couldn't empty " + WString(name) + L". " + SystemErrorText(GetLastError());
    }
    CloseHandle(h);
    return result;
}

static bool IsWorkFileEmpty(const Session& s, const wchar_t* name)
{
    ULONGLONG size;
    FILETIME modified;
    return ProbeFile(WorkPath(s, name), &size, &modified) && size == 0;
}

// Saves, then deletes FSW_PSW.TRC and empties FSW_PSW.MAN, and checks both.
static WString ClearWork(Session* s, SnapKind trcKind, SnapKind manKind, bool keepTrc, bool saveEmptyMan)
{
    bool hasTrc, hasMan;
    String trcSha, manSha;
    WString e = ArchiveWorkFile(s, kTrcName, trcKind, true, &hasTrc, &trcSha, NULL);
    if (e.empty())
        e = ArchiveWorkFile(s, kManName, manKind, saveEmptyMan, &hasMan, &manSha, NULL);
    if (!e.empty())
        return e;

    // Both are saved: only now change WORK.
    if (hasTrc && !keepTrc) {
        e = DeleteIfUnchanged(WorkPath(*s, kTrcName), trcSha, kTrcName);
        if (!e.empty())
            return e;
    }
    if (!(hasMan && IsWorkFileEmpty(*s, kManName))) {
        e = EmptyIfUnchanged(WorkPath(*s, kManName), hasMan, manSha, kManName);
        if (!e.empty())
            return e;
    }

    ULONGLONG size;
    FILETIME modified;
    if (!keepTrc && ProbeFile(WorkPath(*s, kTrcName), &size, &modified))
        return L"A new FSW_PSW.TRC appeared in WORK while it was being cleared. Retry.";
    if (!ProbeFile(WorkPath(*s, kManName), &size, &modified) || size != 0)
        return L"FSW_PSW.MAN isn't empty after emptying it. Retry.";
    return WString();
}

// ---------------------------------------------------------------------------
// session.ini (UTF-8 with BOM, so Notepad shows it)

static String ToUtf8(const WString& w)
{
    if (w.empty())
        return String();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    String out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &out[0], n, NULL, NULL);
    return out;
}

static WString FromUtf8(const char* data, size_t size)
{
    if (!size)
        return WString();
    int n = MultiByteToWideChar(CP_UTF8, 0, data, (int)size, NULL, 0);
    WString out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, data, (int)size, &out[0], n);
    return out;
}

static void Put(WString* out, const wchar_t* key, const WString& value)
{
    WString clean = value;
    for (size_t i = 0; i < clean.size(); ++i) {
        if (clean[i] == L'\r' || clean[i] == L'\n')
            clean[i] = L' ';
    }
    *out += key;
    *out += L'=';
    *out += clean;
    *out += L"\r\n";
}

static void PutInt(WString* out, const wchar_t* key, int value)
{
    Put(out, key, IntText(value));
}

bool SaveSession(Session* s)
{
    WString t = L"; NCS Workbench coding session. The files next to this one are read-only copies.\r\n"
                L"[session]\r\n";
    PutInt(&t, L"version", 1);
    Put(&t, L"id", s->id);
    Put(&t, L"type", kTypeNames[s->type]);
    Put(&t, L"state", kStateNames[s->state]);
    Put(&t, L"work", s->workDir);
    Put(&t, L"started", FormatIsoTime(s->started));
    Put(&t, L"prepared", FormatIsoTime(s->prepared));
    Put(&t, L"reviewedAt", FormatIsoTime(s->reviewedAt));
    Put(&t, L"codedAt", FormatIsoTime(s->codedAt));
    Put(&t, L"finished", FormatIsoTime(s->finished));
    PutInt(&t, L"reuseTrc", s->reuseTrc ? 1 : 0);
    Put(&t, L"chassis", s->chassis);
    Put(&t, L"daten", s->daten);
    Put(&t, L"module", s->module);
    Put(&t, L"vehicle", s->vehicle);
    PutInt(&t, L"reviewed", s->reviewed);
    PutInt(&t, L"coded", s->coded);
    PutInt(&t, L"waitFrom", s->waitFrom);
    Put(&t, L"outcome", kOutcomeNames[s->outcome]);
    PutInt(&t, L"changes", s->changes);
    PutInt(&t, L"verified", s->hasVerify ? 1 : 0);
    PutInt(&t, L"match", s->verify.match);
    PutInt(&t, L"mismatch", s->verify.mismatch);
    PutInt(&t, L"unverifiable", s->verify.unverifiable);
    Put(&t, L"error", s->error);
    for (size_t i = 0; i < s->snaps.size(); ++i) {
        const Snapshot& snap = s->snaps[i];
        t += L"\r\n[snap." + IntText(snap.id) + L"]\r\n";
        Put(&t, L"kind", kKindNames[snap.kind]);
        Put(&t, L"file", snap.file);
        Put(&t, L"sha256", Widen(snap.sha256));
        Put(&t, L"size", IntText((int)snap.size));
        Put(&t, L"taken", FormatIsoTime(snap.taken));
        Put(&t, L"source", snap.source);
    }

    String bytes = "\xEF\xBB\xBF" + ToUtf8(t);
    WString temp = JoinPath(s->dir, kIniTemp), ini = JoinPath(s->dir, kIniName);
    DWORD err = 0;
    HANDLE h = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        err = GetLastError();
    } else {
        DWORD written = 0;
        if (!WriteFile(h, bytes.data(), (DWORD)bytes.size(), &written, NULL) || written != bytes.size() ||
            !FlushFileBuffers(h))
            err = GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
        CloseHandle(h);
    }
    // Replace in one step, so a crash leaves the old record or the new one.
    if (!err && !MoveFileExW(temp.c_str(), ini.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        err = GetLastError();
    if (err) {
        DeleteFileW(temp.c_str());
        s->error = L"Couldn't save the session record in " + s->dir + L". " + SystemErrorText(err);
        return false;
    }
    return true;
}

template <size_t N>
static int Lookup(const wchar_t* const (&names)[N], const WString& value)
{
    for (size_t i = 0; i < N; ++i) {
        if (value == names[i])
            return (int)i;
    }
    return -1;
}

bool LoadSession(const WString& dir, Session* s)
{
    String bytes;
    if (!ReadFileBytes(JoinPath(dir, kIniName), &bytes))
        return false;
    size_t start = bytes.size() >= 3 && bytes.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
    WString text = FromUtf8(bytes.data() + start, bytes.size() - start);

    InitSession(s);
    s->dir = dir;
    std::map<int, Snapshot> snaps;
    int snapId = 0;  // current [snap.N] section, 0 = [session]
    bool inSession = false;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find(L'\n', pos);
        if (end == WString::npos)
            end = text.size();
        WString line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line[line.size() - 1] == L'\r')
            line.erase(line.size() - 1);
        if (line.empty() || line[0] == L';')
            continue;
        if (line[0] == L'[') {
            inSession = line == L"[session]";
            snapId = line.compare(0, 6, L"[snap.") == 0 ? ParseInt(line.substr(6)) : 0;
            if (snapId > 0) {
                snaps[snapId].id = snapId;
                snaps[snapId].kind = SNAP_READ;
            }
            continue;
        }
        size_t eq = line.find(L'=');
        if (eq == WString::npos)
            continue;
        WString key = line.substr(0, eq), value = line.substr(eq + 1);
        if (snapId > 0) {
            Snapshot& snap = snaps[snapId];
            if (key == L"kind") {
                int k = Lookup(kKindNames, value);
                if (k < 0)
                    return false;
                snap.kind = (SnapKind)k;
            } else if (key == L"file") {
                snap.file = value;
            } else if (key == L"sha256") {
                for (size_t i = 0; i < value.size(); ++i)
                    snap.sha256 += (char)value[i];
            } else if (key == L"size") {
                snap.size = (ULONGLONG)ParseInt(value);
            } else if (key == L"taken") {
                snap.taken = ParseIsoTime(value);
            } else if (key == L"source") {
                snap.source = value;
            }
            continue;
        }
        if (!inSession)
            continue;
        if (key == L"id") {
            s->id = value;
        } else if (key == L"type") {
            s->type = value == L"VO" ? SESSION_TYPE_VO : SESSION_TYPE_MAN;
        } else if (key == L"state") {
            int st = Lookup(kStateNames, value);
            s->state = st < 0 ? SESSION_NONE : (SessionState)st;
        } else if (key == L"work") {
            s->workDir = value;
        } else if (key == L"started") {
            s->started = ParseIsoTime(value);
        } else if (key == L"prepared") {
            s->prepared = ParseIsoTime(value);
        } else if (key == L"reviewedAt") {
            s->reviewedAt = ParseIsoTime(value);
        } else if (key == L"codedAt") {
            s->codedAt = ParseIsoTime(value);
        } else if (key == L"finished") {
            s->finished = ParseIsoTime(value);
        } else if (key == L"reuseTrc") {
            s->reuseTrc = value == L"1";
        } else if (key == L"chassis") {
            s->chassis = value;
        } else if (key == L"daten") {
            s->daten = value;
        } else if (key == L"module") {
            s->module = value;
        } else if (key == L"vehicle") {
            s->vehicle = value;
        } else if (key == L"reviewed") {
            s->reviewed = ParseInt(value);
        } else if (key == L"coded") {
            s->coded = ParseInt(value);
        } else if (key == L"waitFrom") {
            s->waitFrom = ParseInt(value);
        } else if (key == L"outcome") {
            int o = Lookup(kOutcomeNames, value);
            s->outcome = o < 0 ? OUTCOME_NONE : (Outcome)o;
        } else if (key == L"changes") {
            s->changes = ParseInt(value);
        } else if (key == L"verified") {
            s->hasVerify = value == L"1";
        } else if (key == L"match") {
            s->verify.match = ParseInt(value);
        } else if (key == L"mismatch") {
            s->verify.mismatch = ParseInt(value);
        } else if (key == L"unverifiable") {
            s->verify.unverifiable = ParseInt(value);
        } else if (key == L"error") {
            s->error = value;
        }
    }
    for (std::map<int, Snapshot>::const_iterator i = snaps.begin(); i != snaps.end(); ++i)
        s->snaps.push_back(i->second);  // map order: by id
    return !s->id.empty() && s->state != SESSION_NONE;
}

static bool NewerFirst(const Session& a, const Session& b)
{
    return a.id > b.id;  // ids are YYYYMMDD-HHMMSS[-n]
}

std::vector<Session> ListSessions(const WString& sessionsRoot)
{
    std::vector<Session> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(JoinPath(sessionsRoot, L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return out;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.')
            continue;
        Session s;
        if (LoadSession(JoinPath(sessionsRoot, fd.cFileName), &s))
            out.push_back(s);
    } while (FindNextFileW(h, &fd) && out.size() < 1000);
    FindClose(h);
    std::sort(out.begin(), out.end(), NewerFirst);
    return out;
}

// ---------------------------------------------------------------------------
// Steps

// Sets the error, saves, and returns the error.
static WString Fail(Session* s, const WString& error)
{
    s->error = error;
    SaveSession(s);
    return error;
}

// Saves after a successful step; returns the save error, if any.
static WString Commit(Session* s)
{
    s->error.clear();
    return SaveSession(s) ? WString() : s->error;
}

static WString WrongState()
{
    return L"That step isn't available at this point of the session.";
}

WString CreateSession(Session* s, const WString& sessionsRoot, const WString& workDir, bool reuseTrc,
                      SessionType type)
{
    InitSession(s);
    WString parent = sessionsRoot.substr(0, sessionsRoot.size() - FileNameOf(sessionsRoot).size());
    DWORD err = EnsureDir(NormalizeDir(parent));
    if (!err)
        err = EnsureDir(sessionsRoot);
    if (err)
        return L"Couldn't create " + sessionsRoot + L". " + SystemErrorText(err);

    GetLocalTime(&s->started);
    wchar_t stamp[32];
    wsprintfW(stamp, L"%04u%02u%02u-%02u%02u%02u", s->started.wYear, s->started.wMonth, s->started.wDay,
              s->started.wHour, s->started.wMinute, s->started.wSecond);
    for (int attempt = 1; attempt <= 99; ++attempt) {
        WString id = stamp;
        if (attempt > 1)
            id += L"-" + IntText(attempt);
        WString dir = JoinPath(sessionsRoot, id);
        if (!CreateDirectoryW(dir.c_str(), NULL)) {
            err = GetLastError();
            if (err == ERROR_ALREADY_EXISTS)
                continue;
            return L"Couldn't create the session folder " + dir + L". " + SystemErrorText(err);
        }
        s->id = id;
        s->dir = dir;
        s->workDir = workDir;
        s->reuseTrc = reuseTrc;
        s->type = type;
        s->state = SESSION_PREPARING;
        s->waitFrom = 1;
        if (!SaveSession(s)) {
            WString e = s->error;
            RemoveDirectoryW(dir.c_str());
            return e;
        }
        return WString();
    }
    return L"Couldn't find a free session folder name in " + sessionsRoot + L".";
}

WString PrepareWorkspace(Session* s)
{
    if (s->state != SESSION_PREPARING)
        return WrongState();
    if (!DirExists(s->workDir))
        return Fail(s, L"The WORK folder " + s->workDir + L" doesn't exist. Check NCS Expert's WORK folder.");

    String trc;
    if (s->reuseTrc) {
        bool exists;
        String sha;
        WString e = ArchiveWorkFile(s, kTrcName, SNAP_BEFORE_TRC, true, &exists, &sha, &trc);
        if (!e.empty())
            return Fail(s, e);
        TraceCheck check;
        ScanTrace(trc.data(), trc.size(), &check);
        if (!exists || !IsPlausibleTrace(check))
            return Fail(s, L"FSW_PSW.TRC is missing or isn't a readable trace, so it can't be the first read. "
                           L"Cancel the session and start again without it.");
        const Snapshot* read = LastSnapshot(*s, SNAP_READ);
        if (!read || read->sha256 != sha) {
            WString stem, tail;
            DefaultSnapshotName(*s, SNAP_READ, &stem, &tail);
            e = WriteSnapshot(s, SNAP_READ, trc, WorkPath(*s, kTrcName), stem, tail);
            if (!e.empty())
                return Fail(s, e);
        }
    }
    WString e = ClearWork(s, SNAP_BEFORE_TRC, SNAP_BEFORE_MAN, s->reuseTrc, true);
    if (!e.empty())
        return Fail(s, e);

    GetLocalTime(&s->prepared);
    s->state = s->reuseTrc ? SESSION_AWAITING_ECU : SESSION_AWAITING_READ;
    s->waitFrom = s->reuseTrc ? LastSnapshot(*s, SNAP_READ)->id : NextSnapshotId(*s);
    s->trcWatch = FileWatch();
    s->manWatch = FileWatch();
    s->notice.clear();
    return Commit(s);
}

// ---------------------------------------------------------------------------
// Watching WORK

enum Settled { SETTLE_ABSENT, SETTLE_WAIT, SETTLE_READY };

// SETTLE_READY with the content once the file has kept its size and time for
// kSettleMs and nobody has it open for writing; once per size/time.
static Settled Settle(FileWatch* w, const WString& path, const wchar_t* name, DWORD now, String* data,
                      WString* notice)
{
    ULONGLONG size;
    FILETIME modified;
    if (!ProbeFile(path, &size, &modified)) {
        *w = FileWatch();
        return SETTLE_ABSENT;
    }
    if (!w->present || w->size != size || CompareFileTime(&w->modified, &modified) != 0) {
        w->present = true;
        w->size = size;
        w->modified = modified;
        w->since = now;
        w->handled = false;
        return SETTLE_WAIT;
    }
    if (w->handled || now - w->since < kSettleMs)
        return SETTLE_WAIT;
    DWORD err = ReadLocked(path, data);
    if (IsBusy(err))
        return SETTLE_WAIT;  // still being written
    if (IsMissing(err)) {
        *w = FileWatch();
        return SETTLE_ABSENT;
    }
    if (err) {
        *notice = L"CAN'T READ " + WString(name) + L" / " + SystemErrorText(err);
        return SETTLE_WAIT;
    }
    if (data->size() != size) {  // changed between the look and the read
        w->present = false;
        return SETTLE_WAIT;
    }
    w->handled = true;
    return SETTLE_READY;
}

static void UpdateVerification(Session* s)
{
    if (s->type == SESSION_TYPE_VO) {
        SessionChanges c;
        if (LoadChanges(*s, &c)) {
            s->hasVerify = true;
            s->changes = c.changes;
        }
        return;
    }
    SessionVerification v;
    if (LoadVerification(*s, &v)) {
        s->hasVerify = true;
        s->verify = v.summary;
    }
}

static void PollTrace(Session* s, DWORD now)
{
    bool readback = s->state == SESSION_AWAITING_READBACK;
    SnapKind kind = readback ? SNAP_READBACK : SNAP_READ;
    String data;
    WString notice;
    Settled r = Settle(&s->trcWatch, WorkPath(*s, kTrcName), kTrcName, now, &data, &notice);
    if (!notice.empty()) {
        s->notice = notice;
        return;
    }
    if (r == SETTLE_ABSENT)
        s->notice.clear();
    if (r != SETTLE_READY)
        return;

    String sha = Sha256Hex(data.data(), data.size());
    const Snapshot* last = LastSnapshotFrom(*s, kind, s->waitFrom);
    if (last && last->sha256 == sha)
        return;
    TraceCheck check;
    ScanTrace(data.data(), data.size(), &check);
    if (!IsPlausibleTrace(check)) {
        s->notice = data.empty() ? L"FSW_PSW.TRC IS EMPTY / NOT TAKEN" : L"FSW_PSW.TRC ISN'T A READABLE TRACE / NOT TAKEN";
        return;
    }
    WString stem, tail;
    DefaultSnapshotName(*s, kind, &stem, &tail);
    WString e = WriteSnapshot(s, kind, data, WorkPath(*s, kTrcName), stem, tail);
    if (!e.empty()) {
        s->trcWatch.handled = false;  // try again on the next look
        Fail(s, e);
        return;
    }
    s->notice.clear();
    if (readback) {
        s->state = SESSION_VERIFYING;
        UpdateVerification(s);
    } else {
        s->state = SESSION_AWAITING_ECU;
    }
    Commit(s);
}

static void PollMan(Session* s, DWORD now)
{
    String data;
    WString notice;
    Settled r = Settle(&s->manWatch, WorkPath(*s, kManName), kManName, now, &data, &notice);
    if (!notice.empty()) {
        s->notice = notice;
        return;
    }
    if (r == SETTLE_ABSENT) {
        s->notice = s->state == SESSION_AWAITING_MAN ? L"" : L"FSW_PSW.MAN IS GONE FROM WORK";
        return;
    }
    if (r != SETTLE_READY)
        return;
    if (data.empty()) {  // emptied, not an export
        s->notice = s->state == SESSION_AWAITING_MAN ? L"" : L"FSW_PSW.MAN IS EMPTY NOW / EXPORT AGAIN";
        return;
    }
    s->notice.clear();
    String sha = Sha256Hex(data.data(), data.size());
    const Snapshot* last = LastSnapshot(*s, SNAP_MAN);
    if (last && last->sha256 == sha) {
        if (s->state == SESSION_AWAITING_MAN) {  // the same export again
            s->state = SESSION_REVIEWING;
            Commit(s);
        }
        return;
    }
    WString stem, tail;
    DefaultSnapshotName(*s, SNAP_MAN, &stem, &tail);
    WString e = WriteSnapshot(s, SNAP_MAN, data, WorkPath(*s, kManName), stem, tail);
    if (!e.empty()) {
        s->manWatch.handled = false;
        Fail(s, e);
        return;
    }
    // A new export after the review (or while coding) needs another review.
    s->state = SESSION_REVIEWING;
    Commit(s);
}

bool PollSession(Session* s, DWORD now)
{
    SessionState state = s->state;
    size_t snaps = s->snaps.size();
    WString notice = s->notice, error = s->error;
    switch (s->state) {
    case SESSION_AWAITING_READ:
    case SESSION_AWAITING_ECU:
    case SESSION_AWAITING_READBACK:
        PollTrace(s, now);
        break;
    case SESSION_AWAITING_MAN:
    case SESSION_REVIEWING:
    case SESSION_CODING:
        if (s->type == SESSION_TYPE_MAN)  // VO takes no export; ConfirmCoded checks the MAN is empty
            PollMan(s, now);
        break;
    default:
        break;
    }
    return s->state != state || s->snaps.size() != snaps || s->notice != notice || s->error != error;
}

// ---------------------------------------------------------------------------
// User actions

WString ConfirmEcu(Session* s, const WString& chassis, const WString& daten, const WString& module,
                   const WString& vehicle)
{
    if (s->state != SESSION_AWAITING_ECU)
        return WrongState();
    const Snapshot* read = LastSnapshotFrom(*s, SNAP_READ, s->waitFrom);
    if (!read)
        return Fail(s, L"There's no captured trace to confirm. Read the ECU again.");
    WString source = SnapshotPath(*s, *read);
    SYSTEMTIME taken = read->taken;
    String data;
    if (!ReadFileBytes(source, &data) || Sha256Hex(data.data(), data.size()) != read->sha256)
        return Fail(s, L"The saved trace " + read->file + L" is missing or changed. Read the ECU again.");

    s->chassis = chassis;
    s->daten = daten;
    s->module = module;
    s->vehicle = vehicle;
    // CHASSIS-MODULE-YYYYMMDD-HHMM-FSW_PSW.TRC: NCS Dummy reads chassis and module from the name.
    WString e = WriteSnapshot(s, SNAP_DUMMY, data, source, daten + L"-" + module + L"-" + FormatFileStamp(taken),
                              L"-FSW_PSW.TRC");
    if (!e.empty())
        return Fail(s, e);
    s->state = s->type == SESSION_TYPE_VO ? SESSION_CODING : SESSION_AWAITING_MAN;
    s->manWatch = FileWatch();
    s->notice.clear();
    return Commit(s);
}

WString ReadAgain(Session* s)
{
    if (s->state != SESSION_AWAITING_ECU)
        return WrongState();
    bool exists;
    String sha;
    WString e = ArchiveWorkFile(s, kTrcName, SNAP_WORK_TRC, true, &exists, &sha, NULL);
    if (e.empty() && exists)
        e = DeleteIfUnchanged(WorkPath(*s, kTrcName), sha, kTrcName);
    if (!e.empty())
        return Fail(s, e);
    s->state = SESSION_AWAITING_READ;
    s->waitFrom = NextSnapshotId(*s);
    s->reuseTrc = false;
    s->trcWatch = FileWatch();
    s->notice.clear();
    return Commit(s);
}

// WORK's MAN must still hold `want`. If not: a new export goes to review, a
// missing or empty one back to waiting for an export.
static WString CheckWorkMan(Session* s, const String& want)
{
    String data;
    DWORD err = ReadLocked(WorkPath(*s, kManName), &data);
    if (IsMissing(err) || (!err && data.empty())) {
        s->state = SESSION_AWAITING_MAN;
        s->manWatch = FileWatch();
        return Fail(s, L"FSW_PSW.MAN is missing or empty now. Export it from NCS Dummy again.");
    }
    if (err)
        return Fail(s, ReadError(kManName, err));
    if (Sha256Hex(data.data(), data.size()) == want)
        return WString();
    WString stem, tail;
    DefaultSnapshotName(*s, SNAP_MAN, &stem, &tail);
    WString e = WriteSnapshot(s, SNAP_MAN, data, WorkPath(*s, kManName), stem, tail);
    if (!e.empty())
        return Fail(s, e);
    s->state = SESSION_REVIEWING;
    s->manWatch = FileWatch();
    return Fail(s, L"FSW_PSW.MAN changed since the review. Check the new export, then continue.");
}

WString ReadyToCode(Session* s)
{
    if (s->state != SESSION_REVIEWING)
        return WrongState();
    const Snapshot* last = LastSnapshot(*s, SNAP_MAN);
    SessionReview review;
    if (!last || !LoadReview(*s, last->id, &review))
        return Fail(s, L"Couldn't read the saved trace or MAN export. Check the session folder.");
    if (!IsPlausibleTrace(review.man))
        return Fail(s, L"This MAN can't be read as functions and values. Check it in NCS Dummy and export again.");
    int id = last->id;
    String sha = last->sha256;
    WString e = CheckWorkMan(s, sha);
    if (!e.empty())
        return e;
    s->reviewed = id;
    GetLocalTime(&s->reviewedAt);
    s->changes = review.changes;
    s->state = SESSION_CODING;
    return Commit(s);
}

WString BackToReview(Session* s)
{
    if (s->state != SESSION_CODING || s->type != SESSION_TYPE_MAN)
        return WrongState();
    s->state = SESSION_REVIEWING;
    return Commit(s);
}

// Saves a non-empty WORK MAN as `kind`, then empties it if it's still the saved content.
static WString SaveAndEmptyMan(Session* s, SnapKind kind)
{
    bool exists;
    String sha;
    WString e = ArchiveWorkFile(s, kManName, kind, false, &exists, &sha, NULL);
    if (e.empty() && exists && !IsWorkFileEmpty(*s, kManName))
        e = EmptyIfUnchanged(WorkPath(*s, kManName), true, sha, kManName);
    return e;
}

// VO: NCS Expert codes a non-empty MAN on top of the VO.
static WString ConfirmVoCoded(Session* s)
{
    String data;
    DWORD err = ReadLocked(WorkPath(*s, kManName), &data);
    if (err && !IsMissing(err))
        return Fail(s, ReadError(kManName, err));
    if (!err && !data.empty())
        return Fail(s, L"FSW_PSW.MAN in WORK isn't empty, and NCS Expert codes it on top of the VO. Empty it, then "
                       L"run SG_CODIEREN (again, if you already coded with it).");
    GetLocalTime(&s->codedAt);
    s->state = SESSION_CODED;
    return Commit(s);
}

WString ConfirmCoded(Session* s)
{
    if (s->type == SESSION_TYPE_VO)
        return s->state == SESSION_CODING ? ConfirmVoCoded(s) : WrongState();
    const Snapshot* reviewed = FindSnapshot(*s, s->reviewed);
    if (s->state != SESSION_CODING || !reviewed)
        return WrongState();
    WString e = CheckWorkMan(s, reviewed->sha256);
    if (!e.empty())
        return e;
    s->coded = s->reviewed;
    GetLocalTime(&s->codedAt);
    s->state = SESSION_CODED;
    return Commit(s);
}

WString EmptyWorkMan(Session* s)
{
    if (s->state != SESSION_CODING || s->type != SESSION_TYPE_VO)
        return WrongState();
    WString e = SaveAndEmptyMan(s, SNAP_WORK_MAN);
    if (!e.empty())
        return Fail(s, e);
    return Commit(s);
}

WString PrepareReadback(Session* s)
{
    if (s->state != SESSION_CODED && s->state != SESSION_VERIFYING)
        return WrongState();
    const Snapshot* coded = FindSnapshot(*s, s->coded);
    if (s->type == SESSION_TYPE_MAN && (!coded || !SnapshotIntact(*s, *coded)))
        return Fail(s, L"The coded MAN's copy in the session folder is missing or changed, so WORK wasn't "
                       L"touched. Check the session folder.");
    WString e = ClearWork(s, SNAP_WORK_TRC, SNAP_WORK_MAN, false, false);
    if (!e.empty())
        return Fail(s, e);
    s->state = SESSION_AWAITING_READBACK;
    s->waitFrom = NextSnapshotId(*s);
    s->trcWatch = FileWatch();
    s->notice.clear();
    return Commit(s);
}

bool OutcomeAllowed(const Session& s, Outcome outcome)
{
    if (s.type == SESSION_TYPE_VO) {
        switch (outcome) {
        case OUTCOME_READ_BACK:
            return s.hasVerify;
        case OUTCOME_UNVERIFIED:
            return !s.hasVerify;
        case OUTCOME_FAILED:
            return true;
        default:
            return false;
        }
    }
    switch (outcome) {
    case OUTCOME_VERIFIED:
        return s.hasVerify && s.verify.mismatch == 0 && s.verify.unverifiable == 0;
    case OUTCOME_DISCREPANCIES:
        return s.hasVerify;
    case OUTCOME_UNVERIFIED:
        return !s.hasVerify;  // a read-back exists: its result is the answer
    case OUTCOME_FAILED:
        return true;
    default:
        return false;
    }
}

Outcome DerivedOutcome(const Session& s)
{
    if (!s.hasVerify)
        return OUTCOME_UNVERIFIED;
    if (s.type == SESSION_TYPE_VO)
        return OUTCOME_READ_BACK;
    return s.verify.mismatch || s.verify.unverifiable ? OUTCOME_DISCREPANCIES : OUTCOME_VERIFIED;
}

WString FinishSession(Session* s, Outcome outcome, bool emptyMan)
{
    if (s->state != SESSION_CODED && s->state != SESSION_AWAITING_READBACK && s->state != SESSION_VERIFYING)
        return WrongState();
    if (!OutcomeAllowed(*s, outcome))
        return Fail(s, L"That outcome doesn't fit the read-back results.");
    if (emptyMan) {
        WString e = SaveAndEmptyMan(s, SNAP_WORK_MAN);
        if (!e.empty())
            return Fail(s, e);
    }
    SessionState was = s->state;
    s->outcome = outcome;
    s->state = outcome == OUTCOME_FAILED ? SESSION_FAILED : SESSION_COMPLETED;
    GetLocalTime(&s->finished);
    WString e = Commit(s);
    if (!e.empty()) {
        s->state = was;
        s->outcome = OUTCOME_NONE;
    }
    return e;
}

WString CancelSession(Session* s)
{
    if (!IsSessionOpen(s->state))
        return WrongState();
    s->outcome = OUTCOME_CANCELLED;
    s->state = SESSION_CANCELLED;
    GetLocalTime(&s->finished);
    return Commit(s);
}

// ---------------------------------------------------------------------------
// Evidence

static bool ReadSnapshot(const Session& s, const Snapshot* snap, String* data)
{
    return snap && ReadFileBytes(SnapshotPath(s, *snap), data);
}

bool LoadReview(const Session& s, int manSnapId, SessionReview* review)
{
    *review = SessionReview();
    const Snapshot* man = manSnapId ? FindSnapshot(s, manSnapId) : LastSnapshot(s, SNAP_MAN);
    String trc, edit;
    if (!ReadSnapshot(s, LastSnapshot(s, SNAP_READ), &trc) || !ReadSnapshot(s, man, &edit))
        return false;
    ScanTrace(trc.data(), trc.size(), &review->trc);
    ScanTrace(edit.data(), edit.size(), &review->man);
    review->rows = DiffTrace(ParseTrace(trc.data(), trc.size()), ParseTrace(edit.data(), edit.size()),
                             &review->summary);
    review->changes = CountIntendedChanges(review->rows);
    review->loaded = true;
    return true;
}

bool LoadVerification(const Session& s, SessionVerification* v)
{
    *v = SessionVerification();
    String trc, man, readback;
    if (!ReadSnapshot(s, LastSnapshot(s, SNAP_READ), &trc) ||
        !ReadSnapshot(s, FindSnapshot(s, s.coded ? s.coded : s.reviewed), &man) ||
        !ReadSnapshot(s, LastSnapshot(s, SNAP_READBACK), &readback))
        return false;
    std::vector<DiffRow> diff = DiffTrace(ParseTrace(trc.data(), trc.size()), ParseTrace(man.data(), man.size()), NULL);
    ScanTrace(readback.data(), readback.size(), &v->readback);
    v->rows = VerifyReadback(diff, ParseTrace(readback.data(), readback.size()), v->readback.conflicting, &v->summary);
    v->loaded = true;
    return true;
}

bool LoadChanges(const Session& s, SessionChanges* c)
{
    *c = SessionChanges();
    String before, after;
    if (!ReadSnapshot(s, LastSnapshot(s, SNAP_READ), &before) ||
        !ReadSnapshot(s, LastSnapshot(s, SNAP_READBACK), &after))
        return false;
    ScanTrace(before.data(), before.size(), &c->before);
    ScanTrace(after.data(), after.size(), &c->after);
    c->rows = DiffTrace(ParseTrace(before.data(), before.size()), ParseTrace(after.data(), after.size()),
                        &c->summary);
    c->summary.partialMan = false;  // both are full reads: a function missing from either is news
    const DiffSummary& d = c->summary;
    c->changes = d.changed + d.onlyTrc + d.onlyMan;
    c->fewShared = (d.same + d.changed) * 2 < d.same + d.changed + d.onlyTrc;
    c->loaded = true;
    return true;
}

// ---------------------------------------------------------------------------
// Export and lock

WString ExportSession(const Session& s, const WString& parent, WString* created)
{
    WString target;
    for (int attempt = 1; attempt <= 99 && target.empty(); ++attempt) {
        WString dir = JoinPath(parent, attempt > 1 ? s.id + L"-" + IntText(attempt) : s.id);
        if (CreateDirectoryW(dir.c_str(), NULL))
            target = dir;
        else if (GetLastError() != ERROR_ALREADY_EXISTS)
            return L"Couldn't create " + dir + L". " + SystemErrorText(GetLastError());
    }
    if (target.empty())
        return L"Couldn't find a free folder name in " + parent + L".";

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(JoinPath(s.dir, L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return L"Couldn't read the session folder " + s.dir + L".";
    WString error;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || lstrcmpiW(fd.cFileName, kIniTemp) == 0)
            continue;
        if (!CopyFileW(JoinPath(s.dir, fd.cFileName).c_str(), JoinPath(target, fd.cFileName).c_str(), TRUE)) {
            error = L"Couldn't copy " + WString(fd.cFileName) + L". " + SystemErrorText(GetLastError());
            break;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    *created = target;
    return error;
}

HANDLE LockWorkDir(const WString& workDir)
{
    WString key = workDir;
    if (!key.empty())
        CharUpperBuffW(&key[0], (DWORD)key.size());
    String utf8 = ToUtf8(key);
    String sha = Sha256Hex(utf8.data(), utf8.size());
    WString name = L"NcsWorkbenchWork-" + Widen(sha.substr(0, 32));
    HANDLE h = CreateMutexW(NULL, FALSE, name.c_str());
    if (h && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(h);
        return NULL;
    }
    return h;
}
