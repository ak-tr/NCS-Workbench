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

    if (map.empty()) {
        for (size_t i = 0; i < folders.size(); ++i)
            out.push_back(std::make_pair(folders[i], folders[i]));
    } else {
        for (size_t i = 0; i < map.size(); ++i) {
            WString chassis = Widen(map[i].first), folder = Widen(map[i].second);
            bool seen = false;
            for (size_t j = 0; j < out.size() && !seen; ++j)
                seen = lstrcmpiW(out[j].first.c_str(), chassis.c_str()) == 0;
            if (!seen && DirExists(JoinPath(datenDir, folder)))
                out.push_back(std::make_pair(chassis, folder));
        }
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

std::vector<unsigned> TraceFunctionIds(const WString& folderPath, const std::vector<TraceEntry>& trace)
{
    // Newest table if there are several (SWTFSW02.DAT, SWTFSW03.DAT).
    std::vector<WString> tables = ListFiles(folderPath, L"SWTFSW*.DAT", false);
    if (tables.empty())
        return std::vector<unsigned>();
    std::sort(tables.begin(), tables.end(), LessNoCase);
    String data;
    if (!ReadFileBytes(JoinPath(folderPath, tables.back()), &data) || data.empty())
        return std::vector<unsigned>();
    std::map<String, unsigned> keywords = ParseKeywordTable((const unsigned char*)data.data(), data.size());

    std::vector<String> names;
    for (size_t i = 0; i < trace.size(); ++i)
        names.push_back(trace[i].name);
    return MapFunctionNames(names, keywords);
}
