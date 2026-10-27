// SP-Daten lookups for the backup dialog: which chassis are installed, the
// module files of a chassis' DATEN folder, and which module a trace came
// from. File formats: src/ncsdata.h.

#pragma once

#include <windows.h>

#include <map>
#include <utility>
#include <vector>

#include "paramindex.h"
#include "trace.h"
#include "xpstd.h"

struct DatenModule {
    WString name;                    // file name, e.g. MK60_87.C0A
    std::vector<unsigned> functions;  // function ids it codes, sorted
};

// DATEN next to the WORK folder (C:\NCSEXPER\WORK -> C:\NCSEXPER\DATEN), or "" if missing.
WString FindDatenDir(const WString& workDir);

// (chassis, DATEN folder) for every chassis whose folder is installed and has
// module files (*.Cxx), sorted by chassis: (E81, E89) ... (E93, E89). Uses
// BR_REF.DAT; an installed folder it doesn't name (or every folder, without
// it) stands for itself.
std::vector<std::pair<WString, WString> > LoadChassisList(const WString& datenDir);
// The DATEN folder of a chassis in that list (any case); "" if it isn't there.
WString FolderForChassis(const std::vector<std::pair<WString, WString> >& list, const WString& chassis);

// Module coding files (*.Cxx) in a DATEN folder, sorted by name. Read one at
// a time with LoadModule so a window can stay responsive (no functions if the
// file can't be read).
std::vector<WString> ListModuleFiles(const WString& folderPath);
DatenModule LoadModule(const WString& folderPath, const WString& name);

// Function ids of the trace's functions, using the folder's SWTFSW*.DAT.
std::vector<unsigned> TraceFunctionIds(const WString& folderPath, const std::vector<TraceEntry>& trace);

// A keyword table (pattern L"SWTFSW*.DAT" or L"SWTPSW*.DAT"): the newest if
// there are several. Empty if missing or unreadable.
std::map<String, unsigned> LoadKeywords(const WString& folderPath, const wchar_t* pattern);

// Parameter Explorer's index of a DATEN folder (paramindex.h), read one
// module file per StepIndexJob so the window can stay responsive.
struct IndexJob {
    WString folderPath;
    std::vector<WString> files;  // module files, sorted
    size_t next = 0;
    ParamIndexBuilder builder;
    std::map<String, String> families;
};

// Lists the folder's module files and reads its ECU families. False if the
// folder can't be listed.
bool BeginIndexJob(const WString& folderPath, IndexJob* job);
// Reads the next module file; false once every file is read.
bool StepIndexJob(IndexJob* job);
// Names the functions and values. Call when done, or when cancelled (then
// the index covers the files read so far and says so).
ParamIndex FinishIndexJob(IndexJob* job, bool cancelled);

// Changes when the folder's files do (names, sizes, times): to tell a
// built index is out of date. Empty if the folder can't be listed.
String FolderSignature(const WString& folderPath);
