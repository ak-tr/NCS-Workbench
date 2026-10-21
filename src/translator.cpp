// Translations service. See translator.h.

#include <windows.h>

#include "translator.h"
#include "util.h"

static const wchar_t kTranslationsName[] = L"Translations.csv";

static TranslationState g_state;
static unsigned g_generation = 1;
static bool g_shown = true, g_first = false, g_settingsRead = false;

WString FindDummyTranslations(const WString& dummyExe)
{
    if (dummyExe.empty())
        return WString();
    WString dir = dummyExe.substr(0, dummyExe.size() - FileNameOf(dummyExe).size());
    dir = NormalizeDir(dir);
    if (dir.empty())
        return WString();
    // Listed rather than opened by name, so a differently cased file is found
    // even in a case-sensitive folder.
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(JoinPath(dir, L"*.csv").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return WString();
    WString found;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && lstrcmpiW(fd.cFileName, kTranslationsName) == 0)
            found = JoinPath(dir, fd.cFileName);
    } while (found.empty() && FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}

static void LoadFile(const WString& path, TranslationState* out)
{
    out->path = path;
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        out->problem = L"The file isn't there any more: " + path;
        return;
    }
    String data;
    if (!ReadFileBytes(path, &data)) {
        DWORD err = GetLastError();
        bool tooBig = info.nFileSizeHigh || info.nFileSizeLow > 8 * 1024 * 1024;
        out->problem = L"Couldn't read " + path + L"." +
                       (tooBig ? WString(L" It's over 8 MB.") : err ? L" " + SystemErrorText(err) : WString());
        return;
    }
    if (!ParseTranslations(data.data(), data.size(), &out->table)) {
        out->problem = FileNameOf(path) + L" isn't a translation list: no keyword,translation rows were found.";
        out->table = TranslationTable();
        return;
    }
    out->loaded = true;
}

void LoadTranslationsWith(const TranslationConfig& config, TranslationState* out)
{
    *out = TranslationState();
    if (!config.file.empty()) {
        out->source = TSOURCE_FILE;
        LoadFile(config.file, out);
        return;
    }
    if (!config.autoFind) {
        out->problem = L"No translation file is chosen, and finding NCS Dummy's is turned off.";
        return;
    }
    if (config.dummyExe.empty()) {
        out->problem = L"NCS Dummy isn't set yet. Choose NcsDummy.exe, or choose a translation file.";
        return;
    }
    out->source = TSOURCE_NCS_DUMMY;
    WString found = FindDummyTranslations(config.dummyExe);
    if (found.empty()) {
        WString dir = config.dummyExe.substr(0, config.dummyExe.size() - FileNameOf(config.dummyExe).size());
        out->path = JoinPath(NormalizeDir(dir), kTranslationsName);
        out->problem = L"There's no Translations.csv next to " + config.dummyExe + L".";
        return;
    }
    LoadFile(found, out);
}

static void ReadDisplaySettings()
{
    g_shown = ReadSetting(L"TranslationsShow", L"1") != L"0";
    g_first = ReadSetting(L"TranslationsOrder") == L"Translation";
    g_settingsRead = true;
}

void LoadTranslations()
{
    ReadDisplaySettings();
    TranslationConfig c;
    c.file = ReadSetting(L"TranslationsPath");
    c.autoFind = ReadSetting(L"TranslationsAuto", L"1") != L"0";
    c.dummyExe = ReadSetting(L"NcsDummyPath");
    LoadTranslationsWith(c, &g_state);
    ++g_generation;
}

const TranslationState& Translations()
{
    return g_state;
}

unsigned TranslationsGeneration()
{
    return g_generation;
}

bool TranslationsShown()
{
    return g_state.loaded && g_shown;
}

bool TranslationFirst()
{
    return g_first;
}

void SetTranslationsShown(bool shown)
{
    if (!g_settingsRead)
        ReadDisplaySettings();
    if (shown == g_shown)
        return;
    g_shown = shown;
    WriteSetting(L"TranslationsShow", shown ? L"1" : L"0");
    ++g_generation;
}

void SetTranslationFirst(bool first)
{
    if (!g_settingsRead)
        ReadDisplaySettings();
    if (first == g_first)
        return;
    g_first = first;
    WriteSetting(L"TranslationsOrder", first ? L"Translation" : L"Keyword");
    ++g_generation;
}

const WString* Translate(const String& keyword)
{
    if (!TranslationsShown() || keyword.empty())
        return NULL;
    return LookupTranslation(g_state.table, Widen(keyword));
}

const WString* TranslateValues(const std::vector<String>& values)
{
    return values.size() == 1 ? Translate(values[0]) : NULL;
}
