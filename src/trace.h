// FSW_PSW.TRC / .MAN parsing and diff. Pure C++, no Windows headers.
//
// Format (see samples/): a function name at column 0, then its value(s),
// each on its own line indented with a tab or spaces; CRLF line endings.
//
//   C0A_V_EINHEIT
//   	wert_01
//   C0A_DF_DIR
//   	wert_00
//   	wert_02

#pragma once

#include <map>
#include <vector>

#include "xpstd.h"

struct TraceEntry {
    String name;
    std::vector<String> values;
};

enum DiffStatus { DIFF_SAME, DIFF_CHANGED, DIFF_ONLY_TRC, DIFF_ONLY_MAN };

struct DiffRow {
    String name;
    std::vector<String> trc, man;  // empty on the side the function is missing from
    DiffStatus status;
};

struct DiffSummary {
    int same = 0, changed = 0, onlyTrc = 0, onlyMan = 0;
    // NCS Expert accepts a MAN listing only the functions to change. When the
    // MAN has under half the TRC's functions, "only in TRC" is the normal case
    // rather than something to flag.
    bool partialMan = false;
};

inline bool IsIndent(char c)
{
    return c == ' ' || c == '\t';
}

// Values before the first function name are ignored, as are blank lines. A
// function listed twice keeps its first occurrence.
inline std::vector<TraceEntry> ParseTrace(const char* data, size_t size)
{
    std::vector<TraceEntry> entries;
    std::map<String, size_t> seen;
    bool skipping = false;  // inside a repeated function
    size_t pos = 0;
    while (pos < size) {
        size_t end = pos;
        while (end < size && data[end] != '\n')
            ++end;
        size_t lineEnd = end;
        if (lineEnd > pos && data[lineEnd - 1] == '\r')
            --lineEnd;

        size_t first = pos;
        while (first < lineEnd && IsIndent(data[first]))
            ++first;
        size_t last = lineEnd;
        while (last > first && IsIndent(data[last - 1]))
            --last;

        if (first < last) {
            String text(data + first, last - first);
            if (first == pos) {  // not indented: a function name
                skipping = seen.count(text) != 0;
                if (!skipping) {
                    seen[text] = entries.size();
                    TraceEntry e;
                    e.name = text;
                    entries.push_back(e);
                }
            } else if (!entries.empty() && !skipping) {
                entries.back().values.push_back(text);
            }
        }
        pos = end + 1;
    }
    return entries;
}

// Rows follow the TRC's order; functions only in the MAN come last, in MAN order.
inline std::vector<DiffRow> DiffTrace(const std::vector<TraceEntry>& trc, const std::vector<TraceEntry>& man,
                                      DiffSummary* summary)
{
    std::map<String, size_t> manIndex;
    for (size_t i = 0; i < man.size(); ++i)
        manIndex[man[i].name] = i;
    std::map<String, bool> inTrc;

    DiffSummary s;
    std::vector<DiffRow> rows;
    for (size_t i = 0; i < trc.size(); ++i) {
        DiffRow row;
        row.name = trc[i].name;
        row.trc = trc[i].values;
        inTrc[row.name] = true;
        std::map<String, size_t>::const_iterator m = manIndex.find(row.name);
        if (m == manIndex.end()) {
            row.status = DIFF_ONLY_TRC;
            ++s.onlyTrc;
        } else {
            row.man = man[m->second].values;
            row.status = row.trc == row.man ? DIFF_SAME : DIFF_CHANGED;
            ++(row.status == DIFF_SAME ? s.same : s.changed);
        }
        rows.push_back(row);
    }
    for (size_t i = 0; i < man.size(); ++i) {
        if (inTrc.count(man[i].name))
            continue;
        DiffRow row;
        row.name = man[i].name;
        row.man = man[i].values;
        row.status = DIFF_ONLY_MAN;
        ++s.onlyMan;
        rows.push_back(row);
    }
    s.partialMan = man.size() * 2 < trc.size();
    if (summary)
        *summary = s;
    return rows;
}

// Rows worth a look: changed, only in the MAN, and only in the TRC unless the
// MAN is a partial one.
inline bool IsNotable(const DiffRow& row, const DiffSummary& s)
{
    return row.status == DIFF_CHANGED || row.status == DIFF_ONLY_MAN ||
           (row.status == DIFF_ONLY_TRC && !s.partialMan);
}

// "wert_00, wert_01"
inline String JoinValues(const std::vector<String>& values)
{
    String out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i)
            out += ", ";
        out += values[i];
    }
    return out;
}
