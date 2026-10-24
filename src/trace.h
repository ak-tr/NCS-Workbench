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

// What ParseTrace skips or can't be sure of. Shown as warnings, never dropped
// silently: a clean parse doesn't make the coding valid, and an odd file gets
// its raw text shown rather than a diff that looks certain.
struct TraceCheck {
    int functions = 0;       // distinct function names
    int strayValues = 0;     // indented lines before the first function name
    int emptyFunctions = 0;  // function names without a value line
    int oddNames = 0;        // names with characters other than A-Z a-z 0-9 _ (comments?)
    int binaryLines = 0;     // control characters: not a text file
    int nonAsciiLines = 0;   // bytes above 0x7E (NCS files are ASCII)
    int firstIssueLine = 0;  // 1-based line of the first of the above, 0 if none
    std::vector<String> duplicates;   // names listed more than once
    std::vector<String> conflicting;  // ...with different values: ambiguous
};

inline bool IsNameChar(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

// Every function in file order, repeats included. `check` may be null.
inline std::vector<TraceEntry> ScanTrace(const char* data, size_t size, TraceCheck* check)
{
    TraceCheck c;
    std::vector<TraceEntry> all;
    int lineNo = 0;
    size_t pos = 0;
    while (pos < size) {
        ++lineNo;
        size_t end = pos;
        while (end < size && data[end] != '\n')
            ++end;
        size_t lineEnd = end;
        if (lineEnd > pos && data[lineEnd - 1] == '\r')
            --lineEnd;

        bool issue = false;
        bool binary = false, nonAscii = false;
        for (size_t i = pos; i < lineEnd; ++i) {
            unsigned char b = (unsigned char)data[i];
            if ((b < 0x20 && b != '\t') || b == 0x7f)
                binary = true;
            else if (b > 0x7e)
                nonAscii = true;
        }
        if (binary) {
            ++c.binaryLines;
            issue = true;
        } else if (nonAscii) {
            ++c.nonAsciiLines;
            issue = true;
        }

        size_t first = pos;
        while (first < lineEnd && IsIndent(data[first]))
            ++first;
        size_t last = lineEnd;
        while (last > first && IsIndent(data[last - 1]))
            --last;
        if (first < last) {
            String text(data + first, last - first);
            if (first == pos) {
                for (size_t i = 0; i < text.size(); ++i) {
                    if (!IsNameChar(text[i])) {
                        ++c.oddNames;
                        issue = true;
                        break;
                    }
                }
                TraceEntry e;
                e.name = text;
                all.push_back(e);
            } else if (all.empty()) {
                ++c.strayValues;
                issue = true;
            } else {
                all.back().values.push_back(text);
            }
        }
        if (issue && !c.firstIssueLine)
            c.firstIssueLine = lineNo;
        pos = end + 1;
    }

    std::map<String, size_t> firstAt;
    std::map<String, bool> flagged, conflict;
    for (size_t i = 0; i < all.size(); ++i) {
        if (all[i].values.empty())
            ++c.emptyFunctions;
        std::map<String, size_t>::const_iterator f = firstAt.find(all[i].name);
        if (f == firstAt.end()) {
            firstAt[all[i].name] = i;
            continue;
        }
        if (!flagged[all[i].name]) {
            flagged[all[i].name] = true;
            c.duplicates.push_back(all[i].name);
        }
        if (all[f->second].values != all[i].values && !conflict[all[i].name]) {
            conflict[all[i].name] = true;
            c.conflicting.push_back(all[i].name);
        }
    }
    c.functions = (int)firstAt.size();
    if (check)
        *check = c;
    return all;
}

// A file worth comparing: some functions, and text rather than binary.
inline bool IsPlausibleTrace(const TraceCheck& c)
{
    return c.functions > 0 && c.binaryLines == 0;
}

// Problems to warn about (duplicates count once per name).
inline int TraceIssueCount(const TraceCheck& c)
{
    return c.strayValues + c.emptyFunctions + c.oddNames + c.binaryLines + c.nonAsciiLines +
           (int)c.duplicates.size();
}

// Values before the first function name are ignored, as are blank lines. A
// function listed twice keeps its first occurrence.
inline std::vector<TraceEntry> ParseTrace(const char* data, size_t size)
{
    std::vector<TraceEntry> all = ScanTrace(data, size, NULL), entries;
    std::map<String, bool> seen;
    for (size_t i = 0; i < all.size(); ++i) {
        if (!seen[all[i].name]) {
            seen[all[i].name] = true;
            entries.push_back(all[i]);
        }
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

// ---------------------------------------------------------------------------
// Read-back verification: did the ECU take the values the MAN asked for?

// What coding the MAN is meant to change: a different value, or a function
// the trace didn't list. A function missing from a partial MAN isn't a change.
inline bool IsIntendedChange(const DiffRow& row)
{
    return row.status == DIFF_CHANGED || row.status == DIFF_ONLY_MAN;
}

inline int CountIntendedChanges(const std::vector<DiffRow>& rows)
{
    int n = 0;
    for (size_t i = 0; i < rows.size(); ++i)
        n += IsIntendedChange(rows[i]) ? 1 : 0;
    return n;
}

enum VerifyStatus { VERIFY_MATCH, VERIFY_MISMATCH, VERIFY_UNVERIFIABLE };

struct VerifyRow {
    String name;
    std::vector<String> original, intended, readback;  // readback empty: not in the read-back
    VerifyStatus status;
};

struct VerifySummary {
    int match = 0, mismatch = 0, unverifiable = 0;
};

// One row per intended change, in diff order. Only an identical value list
// counts as a match; a function missing from the read-back, without values,
// or listed twice with different values there can't be verified.
inline std::vector<VerifyRow> VerifyReadback(const std::vector<DiffRow>& diff, const std::vector<TraceEntry>& readback,
                                             const std::vector<String>& ambiguous, VerifySummary* summary)
{
    std::map<String, size_t> index;
    for (size_t i = 0; i < readback.size(); ++i)
        index[readback[i].name] = i;
    std::map<String, bool> unclear;
    for (size_t i = 0; i < ambiguous.size(); ++i)
        unclear[ambiguous[i]] = true;

    VerifySummary s;
    std::vector<VerifyRow> rows;
    for (size_t i = 0; i < diff.size(); ++i) {
        if (!IsIntendedChange(diff[i]))
            continue;
        VerifyRow row;
        row.name = diff[i].name;
        row.original = diff[i].trc;
        row.intended = diff[i].man;
        std::map<String, size_t>::const_iterator r = index.find(row.name);
        if (r != index.end())
            row.readback = readback[r->second].values;
        if (row.readback.empty() || unclear.count(row.name))
            row.status = VERIFY_UNVERIFIABLE;
        else
            row.status = row.readback == row.intended ? VERIFY_MATCH : VERIFY_MISMATCH;
        ++(row.status == VERIFY_MATCH ? s.match : row.status == VERIFY_MISMATCH ? s.mismatch : s.unverifiable);
        rows.push_back(row);
    }
    if (summary)
        *summary = s;
    return rows;
}
