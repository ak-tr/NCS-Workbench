// SP-Daten lookups. See daten.h.

#include <windows.h>

#include <algorithm>

#include "daten.h"
#include "ncsdata.h"
#include "util.h"

static bool HexDigit(wchar_t c)
{
    return (c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'F') || (c >= L'a' && c <= L'f');
}

// Module coding files end in .C plus two hex digits (.C0A, .C42).
static bool IsModuleFile(const wchar_t* name)
{
    size_t len = lstrlenW(name);
    return len > 4 && name[len - 4] == L'.' && (name[len - 3] == L'C' || name[len - 3] == L'c') &&
           HexDigit(name[len - 2]) && HexDigit(name[len - 1]);
}

static std::vector<WString> ListFiles(const WString& dir, const wchar_t* pattern, bool folders)
{
    std::vector<WString> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(JoinPath(dir, pattern).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return out;
    do {
        bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (isDir == folders && fd.cFileName[0] != L'.')
            out.push_back(fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

static bool LessNoCase(const WString& a, const WString& b)
{
    return lstrcmpiW(a.c_str(), b.c_str()) < 0;
}

WString FindDatenDir(const WString& workDir)
{
    size_t slash = workDir.find_last_of(L'\\');
    if (slash == WString::npos || slash == 0)
        return WString();
    WString daten = JoinPath(workDir.substr(0, slash), L"DATEN");
    return DirExists(daten) ? daten : WString();
}

// At least one module coding file (*.Cxx) in the folder.
static bool HasModuleFiles(const WString& folderPath)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(JoinPath(folderPath, L"*.C*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    bool found = false;
    do {
        found = !(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && IsModuleFile(fd.cFileName);
    } while (!found && FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}

WString FolderForChassis(const std::vector<std::pair<WString, WString> >& list, const WString& chassis)
{
    for (size_t i = 0; i < list.size(); ++i) {
        if (lstrcmpiW(list[i].first.c_str(), chassis.c_str()) == 0)
            return list[i].second;
    }
    return WString();
}

std::vector<std::pair<WString, WString> > LoadChassisList(const WString& datenDir)
{
    std::vector<std::pair<WString, WString> > out;
    if (datenDir.empty())
        return out;
    std::vector<WString> folders = ListFiles(datenDir, L"*", true);

    // BR_REF.DAT is the same in every folder; the first one found will do.
    std::vector<std::pair<String, String> > map;
    for (size_t i = 0; i < folders.size() && map.empty(); ++i) {
        String data;
        WString path = JoinPath(JoinPath(datenDir, folders[i]), L"BR_REF.DAT");
        if (ReadFileBytes(path, &data) && !data.empty())
            map = ParseChassisMap((const unsigned char*)data.data(), data.size());
    }

    // Only folders with coding data count: an empty or half-copied one has nothing to offer.
    std::vector<WString> installed;
    for (size_t i = 0; i < folders.size(); ++i) {
        if (HasModuleFiles(JoinPath(datenDir, folders[i])))
            installed.push_back(folders[i]);
    }
    for (size_t i = 0; i < map.size(); ++i) {
        WString chassis = Widen(map[i].first), folder = Widen(map[i].second);
        const WString* found = NULL;  // the folder as it's spelled on disk
        for (size_t j = 0; j < installed.size() && !found; ++j) {
            if (lstrcmpiW(installed[j].c_str(), folder.c_str()) == 0)
                found = &installed[j];
        }
        if (found && FolderForChassis(out, chassis).empty())
            out.push_back(std::make_pair(chassis, *found));
    }
    // Installed folders BR_REF.DAT doesn't name (or no BR_REF.DAT at all) stand for themselves.
    for (size_t i = 0; i < installed.size(); ++i) {
        bool mapped = false;
        for (size_t j = 0; j < out.size() && !mapped; ++j)
            mapped = lstrcmpiW(out[j].second.c_str(), installed[i].c_str()) == 0;
        if (!mapped && FolderForChassis(out, installed[i]).empty())
            out.push_back(std::make_pair(installed[i], installed[i]));
    }
    std::sort(out.begin(), out.end(),
              [](const std::pair<WString, WString>& a, const std::pair<WString, WString>& b) {
                  return LessNoCase(a.first, b.first);
              });
    return out;
}

std::vector<DatenModule> LoadModules(const WString& folderPath)
{
    std::vector<DatenModule> out;
    std::vector<WString> files = ListFiles(folderPath, L"*.C*", false);
    std::sort(files.begin(), files.end(), LessNoCase);
    for (size_t i = 0; i < files.size(); ++i) {
        if (!IsModuleFile(files[i].c_str()))
            continue;
        DatenModule m;
        m.name = files[i];
        String data;
        if (ReadFileBytes(JoinPath(folderPath, files[i]), &data) && !data.empty())
            m.functions = ParseModuleFunctions((const unsigned char*)data.data(), data.size());
        out.push_back(m);
    }
    return out;
}

std::map<String, unsigned> LoadKeywords(const WString& folderPath, const wchar_t* pattern)
{
    // Newest table if there are several (SWTFSW02.DAT, SWTFSW03.DAT).
    std::vector<WString> tables = ListFiles(folderPath, pattern, false);
    if (tables.empty())
        return std::map<String, unsigned>();
    std::sort(tables.begin(), tables.end(), LessNoCase);
    String data;
    if (!ReadFileBytes(JoinPath(folderPath, tables.back()), &data) || data.empty())
        return std::map<String, unsigned>();
    return ParseKeywordTable((const unsigned char*)data.data(), data.size());
}

std::vector<unsigned> TraceFunctionIds(const WString& folderPath, const std::vector<TraceEntry>& trace)
{
    std::map<String, unsigned> keywords = LoadKeywords(folderPath, L"SWTFSW*.DAT");
    if (keywords.empty())
        return std::vector<unsigned>();
    std::vector<String> names;
    for (size_t i = 0; i < trace.size(); ++i)
        names.push_back(trace[i].name);
    return MapFunctionNames(names, keywords);
}

// File names are ASCII in SP-Daten; anything else can't be one of its files.
static String NarrowName(const WString& name)
{
    String out;
    for (size_t i = 0; i < name.size(); ++i)
        out += name[i] < 0x80 ? (char)name[i] : '?';
    return out;
}

bool BeginIndexJob(const WString& folderPath, IndexJob* job)
{
    job->folderPath = folderPath;
    job->next = 0;
    job->builder = ParamIndexBuilder();
    job->files.clear();
    job->families.clear();
    if (!DirExists(folderPath))
        return false;
    std::vector<WString> files = ListFiles(folderPath, L"*.C*", false);
    std::sort(files.begin(), files.end(), LessNoCase);
    for (size_t i = 0; i < files.size(); ++i) {
        if (IsModuleFile(files[i].c_str()))
            job->files.push_back(files[i]);
    }
    job->builder.index.moduleFiles = (int)job->files.size();
    // ECU families: <folder>SGET.000 (E89SGET.000); any *SGET.000 will do.
    std::vector<WString> sget = ListFiles(folderPath, L"*SGET.000", false);
    for (size_t i = 0; i < sget.size() && job->families.empty(); ++i) {
        String data;
        if (ReadFileBytes(JoinPath(folderPath, sget[i]), &data) && !data.empty())
            job->families = ParseModuleFamilies((const unsigned char*)data.data(), data.size());
    }
    return true;
}

bool StepIndexJob(IndexJob* job)
{
    if (job->next >= job->files.size())
        return false;
    const WString& name = job->files[job->next++];
    String data;
    if (ReadFileBytes(JoinPath(job->folderPath, name), &data))
        AddModuleToIndex(&job->builder, NarrowName(name), (const unsigned char*)data.data(), data.size(),
                         job->families);
    else
        AddUnreadableModule(&job->builder, NarrowName(name));
    return job->next < job->files.size();
}

ParamIndex FinishIndexJob(IndexJob* job, bool cancelled)
{
    std::map<String, unsigned> fsw = LoadKeywords(job->folderPath, L"SWTFSW*.DAT");
    std::map<String, unsigned> psw = LoadKeywords(job->folderPath, L"SWTPSW*.DAT");
    ParamIndex x = FinishIndex(&job->builder, fsw, psw);
    x.cancelled = cancelled && job->next < job->files.size();
    return x;
}

String FolderSignature(const WString& folderPath)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(JoinPath(folderPath, L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return String();
    // Order-independent sums: FindNextFile's order isn't guaranteed.
    unsigned long long count = 0, sizes = 0, times = 0, names = 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        ++count;
        sizes += ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        times += ((unsigned long long)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
        unsigned long long hash = 1469598103934665603ULL;  // FNV-1a of the upper-case name
        for (const wchar_t* c = fd.cFileName; *c; ++c)
            hash = (hash ^ (unsigned)CharUpperW((LPWSTR)(ULONG_PTR)(unsigned short)*c)) * 1099511628211ULL;
        names += hash;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    String out;
    unsigned long long parts[4] = { count, sizes, times, names };
    for (int p = 0; p < 4; ++p) {
        for (int shift = 60; shift >= 0; shift -= 4)
            out += "0123456789abcdef"[(parts[p] >> shift) & 0xF];
        out += '-';
    }
    return out;
}
