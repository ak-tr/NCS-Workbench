// SP-Daten lookups for the backup dialog: which chassis are installed, the
// module files of a chassis' DATEN folder, and which module a trace came
// from. File formats: src/ncsdata.h.

#pragma once

#include <windows.h>

#include <utility>
#include <vector>

#include "trace.h"
#include "xpstd.h"

struct DatenModule {
    WString name;                    // file name, e.g. MK60_87.C0A
    std::vector<unsigned> functions;  // function ids it codes, sorted
};

// DATEN next to the WORK folder (C:\NCSEXPER\WORK -> C:\NCSEXPER\DATEN), or "" if missing.
WString FindDatenDir(const WString& workDir);

// (chassis, DATEN folder) for every chassis whose folder is installed, sorted
// by chassis: (E81, E89) ... (E93, E89). Uses BR_REF.DAT; without it, each
// installed folder stands for itself.
std::vector<std::pair<WString, WString> > LoadChassisList(const WString& datenDir);

// Module coding files (*.Cxx) in a DATEN folder, sorted by name.
std::vector<DatenModule> LoadModules(const WString& folderPath);

// Function ids of the trace's functions, using the folder's SWTFSW*.DAT.
std::vector<unsigned> TraceFunctionIds(const WString& folderPath, const std::vector<TraceEntry>& trace);
