// NCS Expert SP-Daten files (DATEN\<folder>\). Pure C++, no Windows headers.
//
// Every file is a list of records: length (1 byte), type (2 bytes, little
// endian), payload (length bytes), a check byte. The header defines tables:
// type 0x0300 = table id (u16) + name, 0x0400 = field format, 0x0500 = field
// names, 0xFF00 = end of header. Data records use the table id as their type.
//
// Files used:
//   BR_REF.DAT     chassis -> shared DATEN folder (E91 -> E89)
//   SWTFSW*.DAT    function name -> id
//   SWTPSW*.DAT    value name -> id (same format)
//   <folder>SGET.000  module file -> ECU family (MK60_87.C0A -> DSC)
//   <module>.Cxx   a module's coding data, incl. the function ids it codes.
//                  The name is SGNAME.C + the coding index in hex; the file
//                  says the index again in SGID_CODIERINDEX.
//
// In a module file, each PARZUWEISUNG_FSW record (a function with a list of
// values) is followed by its values: PARZUWEISUNG_PSW1 records (value id +
// data), with PARZUWEISUNG_PSW2 records (more data) in between.
// PARZUWEISUNG_DIR records are functions coded with a direct number instead.
// Checked against a full E89 folder: 397 modules, all with these formats.

#pragma once

#include <algorithm>
#include <map>
#include <utility>
#include <vector>

#include "xpstd.h"

struct NcsRecord {
    unsigned type;
    const unsigned char* data;
    size_t size;
};

inline std::vector<NcsRecord> ParseNcsRecords(const unsigned char* p, size_t n)
{
    std::vector<NcsRecord> out;
    size_t i = 0;
    while (i + 3 <= n) {
        size_t len = p[i];
        if (i + 3 + len + 1 > n)
            break;  // trailing bytes after the last record
        NcsRecord r = { (unsigned)(p[i + 1] | (p[i + 2] << 8)), p + i + 3, len };
        out.push_back(r);
        i += 3 + len + 1;
    }
    return out;
}

inline String NcsString(const NcsRecord& r, size_t start)
{
    if (start >= r.size)
        return String();
    size_t end = start;
    while (end < r.size && r.data[end])
        ++end;
    return String((const char*)r.data + start, end - start);
}

inline unsigned NcsWord(const NcsRecord& r, size_t at)
{
    return at + 2 <= r.size ? (unsigned)(r.data[at] | (r.data[at + 1] << 8)) : 0;
}

struct NcsTable {
    int id;  // -1 if the file doesn't define the table
    String format;
    String fields;  // field names, comma-separated
};

inline NcsTable FindNcsTable(const std::vector<NcsRecord>& recs, const char* name)
{
    NcsTable t = { -1, String(), String() };
    bool current = false;
    for (size_t i = 0; i < recs.size(); ++i) {
        const NcsRecord& r = recs[i];
        if ((r.type & 0xFF) != 0)
            continue;  // data record
        unsigned kind = r.type >> 8;
        if (kind == 0xFF)
            break;
        if (kind == 3) {
            current = NcsString(r, 2) == name;
            if (current)
                t.id = (int)NcsWord(r, 0);
        } else if (kind == 4 && current) {
            t.format = NcsString(r, 0);
        } else if (kind == 5 && current) {
            t.fields = NcsString(r, 0);
        }
    }
    return t;
}

// SWTFSW*.DAT: function name -> id (table SWT_EINTRAG, format "WS").
inline std::map<String, unsigned> ParseKeywordTable(const unsigned char* p, size_t n)
{
    std::map<String, unsigned> out;
    std::vector<NcsRecord> recs = ParseNcsRecords(p, n);
    NcsTable t = FindNcsTable(recs, "SWT_EINTRAG");
    if (t.id <= 0 || t.format != "WS")
        return out;
    for (size_t i = 0; i < recs.size(); ++i) {
        if (recs[i].type == (unsigned)t.id && recs[i].size > 2)
            out[NcsString(recs[i], 2)] = NcsWord(recs[i], 0);
    }
    return out;
}

// id -> name, from ParseKeywordTable's name -> id.
inline std::map<unsigned, String> InvertKeywords(const std::map<String, unsigned>& keywords)
{
    std::map<unsigned, String> out;
    for (std::map<String, unsigned>::const_iterator i = keywords.begin(); i != keywords.end(); ++i)
        out.insert(std::make_pair(i->second, i->first));  // an id listed twice keeps its first name
    return out;
}

struct ModuleFunction {
    unsigned id;
    bool direct;                  // PARZUWEISUNG_DIR: coded with a number, no list of values
    std::vector<unsigned> values;  // value (PSW) ids, in file order, unique
};

enum ModuleStatus {
    MODULE_OK,
    MODULE_NOT_NCS,       // no records or no table header: not an SP-Daten file
    MODULE_NO_FUNCTIONS,  // no PARZUWEISUNG_FSW table
    MODULE_UNSUPPORTED,   // the table is there, in a format this parser doesn't know
};

struct ModuleCoding {
    ModuleStatus status = MODULE_NOT_NCS;
    int codingIndex = -1;  // SGID_CODIERINDEX, -1 if missing
    std::vector<ModuleFunction> functions;  // file order; a function listed twice is merged
};

// Function id of a PARZUWEISUNG_FSW / _DIR record, format "{L}LWW...": a
// counted list of 4-byte values (count byte first), a 4-byte value, a word,
// then the function id (word). False if the record is too short.
inline bool ModuleRecordFunction(const NcsRecord& r, unsigned* id)
{
    if (r.size == 0)
        return false;
    size_t at = 1 + 4 * (size_t)r.data[0] + 4 + 2;
    if (at + 2 > r.size)
        return false;
    *id = NcsWord(r, at);
    return true;
}

// Module coding file (*.Cxx): its coding index, functions and their values.
inline ModuleCoding ParseModuleCoding(const unsigned char* p, size_t n)
{
    ModuleCoding m;
    std::vector<NcsRecord> recs = ParseNcsRecords(p, n);
    NcsTable fsw = FindNcsTable(recs, "PARZUWEISUNG_FSW");
    if (fsw.id < 0) {
        bool header = false;
        for (size_t i = 0; i < recs.size() && !header; ++i)
            header = recs[i].type == 0xFF00;
        m.status = header ? MODULE_NO_FUNCTIONS : MODULE_NOT_NCS;
        return m;
    }
    if (fsw.id == 0 || fsw.format.compare(0, 6, "{L}LWW") != 0) {
        m.status = MODULE_UNSUPPORTED;
        return m;
    }
    m.status = MODULE_OK;
    NcsTable dir = FindNcsTable(recs, "PARZUWEISUNG_DIR");
    NcsTable psw1 = FindNcsTable(recs, "PARZUWEISUNG_PSW1");
    NcsTable psw2 = FindNcsTable(recs, "PARZUWEISUNG_PSW2");
    NcsTable index = FindNcsTable(recs, "SGID_CODIERINDEX");
    bool dirOk = dir.id > 0 && dir.format.compare(0, 6, "{L}LWW") == 0;
    bool valuesOk = psw1.id > 0 && psw1.format.compare(0, 1, "W") == 0;

    std::map<unsigned, size_t> where;  // function id -> index in m.functions
    int current = -1;                  // function the next PSW1 records belong to
    for (size_t i = 0; i < recs.size(); ++i) {
        const NcsRecord& r = recs[i];
        if ((r.type & 0xFF) == 0 && (r.type >> 8) != 0)
            continue;  // header record
        unsigned id;
        bool isFsw = r.type == (unsigned)fsw.id, isDir = dirOk && r.type == (unsigned)dir.id;
        if ((isFsw || isDir) && ModuleRecordFunction(r, &id)) {
            std::map<unsigned, size_t>::iterator w = where.find(id);
            if (w == where.end()) {
                ModuleFunction f;
                f.id = id;
                f.direct = isDir;
                w = where.insert(std::make_pair(id, m.functions.size())).first;
                m.functions.push_back(f);
            } else if (isFsw) {
                m.functions[w->second].direct = false;  // a list of values after all
            }
            current = isFsw ? (int)w->second : -1;
        } else if (valuesOk && r.type == (unsigned)psw1.id) {
            if (current >= 0 && r.size >= 2) {
                std::vector<unsigned>& v = m.functions[current].values;
                unsigned value = NcsWord(r, 0);
                if (std::find(v.begin(), v.end(), value) == v.end())
                    v.push_back(value);
            }
        } else if (psw2.id > 0 && r.type == (unsigned)psw2.id) {
            // more data for the value before it
        } else if (index.id > 0 && r.type == (unsigned)index.id) {
            if (r.size >= 1 && m.codingIndex < 0)
                m.codingIndex = r.data[0];
            current = -1;
        } else {
            current = -1;  // another table: the run of values has ended
        }
    }
    return m;
}

// Module coding file (*.Cxx): ids of the functions it codes from its
// PARZUWEISUNG_FSW table, sorted and unique (module detection).
inline std::vector<unsigned> ParseModuleFunctions(const unsigned char* p, size_t n)
{
    std::vector<unsigned> ids;
    ModuleCoding m = ParseModuleCoding(p, n);
    for (size_t i = 0; i < m.functions.size(); ++i) {
        if (!m.functions[i].direct)
            ids.push_back(m.functions[i].id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

// Splits "A,B,C".
inline std::vector<String> SplitFieldNames(const String& s)
{
    std::vector<String> out;
    size_t start = 0;
    for (;;) {
        size_t comma = s.find(',', start);
        out.push_back(s.substr(start, comma == String::npos ? String::npos : comma - start));
        if (comma == String::npos)
            return out;
        start = comma + 1;
    }
}

// <folder>SGET.000: module file name -> ECU family, e.g. MK60_87.C0A -> DSC.
// Tables SGAUSWAHL_* list SGNAME (MK60_87), CBD (C0A) and UMRSG (DSC) among
// their leading string fields. Keys are upper case.
inline std::map<String, String> ParseModuleFamilies(const unsigned char* p, size_t n)
{
    std::map<String, String> out;
    std::vector<NcsRecord> recs = ParseNcsRecords(p, n);
    static const char* const kTables[] = { "SGAUSWAHL_SGBD", "SGAUSWAHL_VM", "SGAUSWAHL_VMSGBD" };
    for (size_t k = 0; k < sizeof(kTables) / sizeof(kTables[0]); ++k) {
        NcsTable t = FindNcsTable(recs, kTables[k]);
        if (t.id <= 0)
            continue;
        std::vector<String> names = SplitFieldNames(t.fields);
        size_t strings = 0;  // leading string fields, readable one after another
        while (strings < t.format.size() && t.format[strings] == 'S')
            ++strings;
        int sg = -1, cbd = -1, family = -1;
        for (size_t i = 0; i < names.size() && i < strings; ++i) {
            if (names[i] == "SGNAME")
                sg = (int)i;
            else if (names[i] == "CBD")
                cbd = (int)i;
            else if (names[i] == "UMRSG")
                family = (int)i;
        }
        if (sg < 0 || cbd < 0 || family < 0)
            continue;
        for (size_t i = 0; i < recs.size(); ++i) {
            if (recs[i].type != (unsigned)t.id)
                continue;
            std::vector<String> values;
            size_t at = 0;
            for (size_t f = 0; f < strings; ++f) {
                String s = NcsString(recs[i], at);
                values.push_back(s);
                at += s.size() + 1;
            }
            String file = values[sg] + "." + values[cbd];
            for (size_t c = 0; c < file.size(); ++c) {
                if (file[c] >= 'a' && file[c] <= 'z')
                    file[c] = char(file[c] - 'a' + 'A');
            }
            if (!values[sg].empty() && !values[cbd].empty() && !values[family].empty())
                out.insert(std::make_pair(file, values[family]));
        }
    }
    return out;
}

// BR_REF.DAT: (chassis, DATEN folder) pairs, e.g. (E91, E89). Table BR_ERSATZ, format "SS".
inline std::vector<std::pair<String, String> > ParseChassisMap(const unsigned char* p, size_t n)
{
    std::vector<std::pair<String, String> > out;
    std::vector<NcsRecord> recs = ParseNcsRecords(p, n);
    NcsTable t = FindNcsTable(recs, "BR_ERSATZ");
    if (t.id <= 0 || t.format != "SS")
        return out;
    for (size_t i = 0; i < recs.size(); ++i) {
        if (recs[i].type != (unsigned)t.id)
            continue;
        String chassis = NcsString(recs[i], 0);
        String folder = NcsString(recs[i], chassis.size() + 1);
        if (!chassis.empty() && !folder.empty())
            out.push_back(std::make_pair(chassis, folder));
    }
    return out;
}

// Ids of the trace's function names; names the table doesn't know are skipped.
inline std::vector<unsigned> MapFunctionNames(const std::vector<String>& names,
                                              const std::map<String, unsigned>& keywords)
{
    std::vector<unsigned> ids;
    for (size_t i = 0; i < names.size(); ++i) {
        std::map<String, unsigned>::const_iterator k = keywords.find(names[i]);
        if (k != keywords.end())
            ids.push_back(k->second);
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

// How many of the trace's function ids a module codes (both sorted).
inline int CountSharedFunctions(const std::vector<unsigned>& trace, const std::vector<unsigned>& module)
{
    int hits = 0;
    for (size_t i = 0; i < trace.size(); ++i) {
        if (std::binary_search(module.begin(), module.end(), trace[i]))
            ++hits;
    }
    return hits;
}

// Index of the module that clearly matches a trace with traceCount functions,
// given each module's hits (CountSharedFunctions) and size (functions it codes).
// A module's differences are the trace's functions it lacks plus the ones it
// codes that the trace doesn't. The match codes at least 80% of the trace's
// functions and has under half the differences of every other module. Counting
// both sides tells coding indexes of one ECU apart: KMBI_PL2.C08 and .C09 both
// have all 316 functions of a C08 trace, but C09 codes 66 the trace lacks, C08 13.
// -1 if no module is a clear match.
inline int PickModule(const std::vector<int>& hits, const std::vector<int>& sizes, int traceCount)
{
    if (traceCount <= 0 || hits.size() != sizes.size())
        return -1;
    int best = -1, bestDiff = 0, runnerUp = -1;  // runner-up: the fewest differences after best
    for (size_t i = 0; i < hits.size(); ++i) {
        int diff = (traceCount - hits[i]) + (sizes[i] - hits[i]);
        if (best < 0 || diff < bestDiff) {
            if (best >= 0)
                runnerUp = bestDiff;
            best = (int)i;
            bestDiff = diff;
        } else if (runnerUp < 0 || diff < runnerUp) {
            runnerUp = diff;
        }
    }
    if (best < 0 || hits[best] * 5 < traceCount * 4 || (runnerUp >= 0 && bestDiff * 2 >= runnerUp))
        return -1;
    return best;
}
