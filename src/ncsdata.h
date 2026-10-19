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
//   <module>.Cxx   a module's coding data, incl. the function ids it codes

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
};

inline NcsTable FindNcsTable(const std::vector<NcsRecord>& recs, const char* name)
{
    NcsTable t = { -1, String() };
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

// Module coding file (*.Cxx): ids of the functions it codes, sorted and unique.
// Table PARZUWEISUNG_FSW, format "{L}LWW...": a counted list of 4-byte values
// (count byte first), a 4-byte value, a word, then the function id (word).
inline std::vector<unsigned> ParseModuleFunctions(const unsigned char* p, size_t n)
{
    std::vector<unsigned> ids;
    std::vector<NcsRecord> recs = ParseNcsRecords(p, n);
    NcsTable t = FindNcsTable(recs, "PARZUWEISUNG_FSW");
    if (t.id <= 0 || t.format.compare(0, 6, "{L}LWW") != 0)
        return ids;
    for (size_t i = 0; i < recs.size(); ++i) {
        const NcsRecord& r = recs[i];
        if (r.type != (unsigned)t.id || r.size == 0)
            continue;
        size_t at = 1 + 4 * (size_t)r.data[0] + 4 + 2;
        if (at + 2 <= r.size)
            ids.push_back(NcsWord(r, at));
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
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

// Index of the module that clearly matches a trace with traceCount functions:
// it codes at least 80% of them and more than twice as many as the runner-up.
// -1 if no module is a clear match.
inline int PickModule(const std::vector<int>& hits, int traceCount)
{
    int best = -1, runnerUp = 0;
    for (size_t i = 0; i < hits.size(); ++i) {
        if (best < 0 || hits[i] > hits[best]) {
            if (best >= 0)
                runnerUp = hits[best];
            best = (int)i;
        } else if (hits[i] > runnerUp) {
            runnerUp = hits[i];
        }
    }
    if (best < 0 || traceCount <= 0 || hits[best] * 5 < traceCount * 4 || hits[best] <= runnerUp * 2)
        return -1;
    return best;
}
