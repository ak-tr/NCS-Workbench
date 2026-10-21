// NCS Dummy's Translations.csv: English names for BMW coding keywords.
// Pure C++, no Windows headers.
//
// The file belongs to the user's NCS Dummy install (translations by revtor
// and contributors); Workbench only reads it, never ships or changes it.
// Format, from the file NCS Dummy ships (26,243 rows, last modified
// 20201210):
//
//   CONTRIBUTORS,"revtor,IcemanBHE,..."     metadata rows, not keywords
//   LASTMODIFIED,20201210
//                                           blank lines are skipped
//   aktiv,Enabled                           keyword,translation
//   0_004nms/grad,                          an empty translation = none
//   04bh,"Check control notification ""Trailer tow bar electrics!"""
//
// One row per line, comma-separated, a field in double quotes may hold
// commas and "" for a quote; CRLF line endings; Windows-1252 (UTF-8 and UTF-16
// with a byte order mark, or UTF-8 without one, are read too). Keywords keep
// their exact spelling and case: lookups are exact, only searches ignore case.

#pragma once

#include <map>
#include <vector>

#include "xpstd.h"

enum TextEncoding { TEXT_CP1252, TEXT_UTF8, TEXT_UTF16LE };

// Bytes 0x80-0x9F of Windows-1252; 0 = not defined (0x81, 0x8D, 0x8F, 0x90, 0x9D).
inline wchar_t Cp1252High(unsigned char b)
{
    static const unsigned short kTable[32] = {
        0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
        0x2039, 0x0152, 0,      0x017D, 0,      0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
        0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178,
    };
    return (wchar_t)kTable[b - 0x80];
}

// Length of the UTF-8 sequence at p (1-4), or 0 if it isn't valid UTF-8.
inline size_t Utf8Sequence(const unsigned char* p, size_t n, unsigned* cp)
{
    unsigned char b = p[0];
    size_t len = b < 0x80 ? 1 : (b & 0xE0) == 0xC0 ? 2 : (b & 0xF0) == 0xE0 ? 3 : (b & 0xF8) == 0xF0 ? 4 : 0;
    if (!len || len > n)
        return 0;
    unsigned c = len == 1 ? b : len == 2 ? (b & 0x1Fu) : len == 3 ? (b & 0x0Fu) : (b & 0x07u);
    for (size_t i = 1; i < len; ++i) {
        if ((p[i] & 0xC0) != 0x80)
            return 0;
        c = (c << 6) | (p[i] & 0x3Fu);
    }
    static const unsigned kMin[5] = { 0, 0, 0x80, 0x800, 0x10000 };
    if (c < kMin[len] || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF))
        return 0;  // overlong, out of range, surrogate
    *cp = c;
    return len;
}

inline bool IsValidUtf8(const unsigned char* p, size_t n, bool* anyMultibyte)
{
    *anyMultibyte = false;
    for (size_t i = 0; i < n;) {
        unsigned cp;
        size_t len = Utf8Sequence(p + i, n - i, &cp);
        if (!len)
            return false;
        if (len > 1)
            *anyMultibyte = true;
        i += len;
    }
    return true;
}

// Bytes -> UTF-16 text. A byte order mark picks UTF-8 or UTF-16LE; without
// one, the file is UTF-8 only if it is valid UTF-8 with at least one
// multi-byte character, else Windows-1252. Characters that can't be decoded
// (a byte Windows-1252 doesn't define, bad UTF-8 after a BOM, a lone
// surrogate) become U+FFFD and are counted in *undecodable.
inline WString DecodeText(const char* data, size_t size, TextEncoding* encoding, int* undecodable)
{
    const unsigned char* p = (const unsigned char*)data;
    WString out;
    *undecodable = 0;
    if (size >= 2 && p[0] == 0xFF && p[1] == 0xFE) {
        *encoding = TEXT_UTF16LE;
        for (size_t i = 2; i + 1 < size; i += 2) {
            wchar_t c = (wchar_t)(p[i] | (p[i + 1] << 8));
            bool high = c >= 0xD800 && c <= 0xDBFF, low = c >= 0xDC00 && c <= 0xDFFF;
            if (high && i + 3 < size) {
                wchar_t next = (wchar_t)(p[i + 2] | (p[i + 3] << 8));
                if (next >= 0xDC00 && next <= 0xDFFF) {
                    out += c;
                    out += next;
                    i += 2;
                    continue;
                }
            }
            if (high || low) {
                out += (wchar_t)0xFFFD;
                ++*undecodable;
            } else {
                out += c;
            }
        }
        if (size % 2)
            ++*undecodable;  // half a character at the end
        return out;
    }
    size_t start = 0;
    bool multibyte = false;
    if (size >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) {
        *encoding = TEXT_UTF8;
        start = 3;
    } else {
        *encoding = IsValidUtf8(p, size, &multibyte) && multibyte ? TEXT_UTF8 : TEXT_CP1252;
    }
    out.reserve(size - start);
    for (size_t i = start; i < size;) {
        unsigned char b = p[i];
        if (b < 0x80) {
            out += (wchar_t)b;
            ++i;
        } else if (*encoding == TEXT_CP1252) {
            wchar_t c = b >= 0xA0 ? (wchar_t)b : Cp1252High(b);
            if (!c) {
                c = (wchar_t)0xFFFD;
                ++*undecodable;
            }
            out += c;
            ++i;
        } else {
            unsigned cp;
            size_t len = Utf8Sequence(p + i, size - i, &cp);
            if (!len) {
                out += (wchar_t)0xFFFD;
                ++*undecodable;
                ++i;
            } else if (cp >= 0x10000) {
                cp -= 0x10000;
                out += (wchar_t)(0xD800 + (cp >> 10));
                out += (wchar_t)(0xDC00 + (cp & 0x3FF));
                i += len;
            } else {
                out += (wchar_t)cp;
                i += len;
            }
        }
    }
    return out;
}

// One CSV record: the line at text[*pos] (ended by CRLF, LF or a lone CR).
// A field in double quotes may hold commas and "" for a quote, but not a
// line break: NCS Dummy's file has none, and so a missing closing quote
// costs only its own line instead of swallowing the rows after it. Returns
// false if the line is malformed: a quote that doesn't close on it, or text
// after a closing quote. *pos moves to the next line either way. A quote
// inside an unquoted field is kept as is.
inline bool ReadCsvRecord(const WString& text, size_t* pos, std::vector<WString>* fields)
{
    fields->clear();
    size_t n = text.size(), end = *pos;
    while (end < n && text[end] != L'\r' && text[end] != L'\n')
        ++end;
    size_t i = *pos;
    *pos = end < n && text[end] == L'\r' ? end + 1 : end;
    if (*pos < n && text[*pos] == L'\n')
        ++*pos;
    bool ok = true;
    for (;;) {
        WString field;
        if (i < end && text[i] == L'"') {
            ++i;
            bool closed = false;
            while (i < end && !closed) {
                wchar_t c = text[i++];
                if (c != L'"') {
                    field += c;
                } else if (i < end && text[i] == L'"') {
                    field += L'"';
                    ++i;
                } else {
                    closed = true;
                }
            }
            if (!closed)
                return false;
            // Only a delimiter or the end of the line may follow a closing quote.
            if (i < end && text[i] != L',') {
                ok = false;
                while (i < end && text[i] != L',')
                    ++i;
            }
        } else {
            while (i < end && text[i] != L',')
                field += text[i++];
        }
        fields->push_back(field);
        if (i >= end)
            return ok;
        ++i;  // the comma
    }
}

struct TranslationTable {
    std::map<WString, WString> entries;  // keyword -> translation; usable (non-empty) ones only
    WString contributors, lastModified;  // CONTRIBUTORS / LASTMODIFIED rows
    TextEncoding encoding = TEXT_CP1252;
    int keywords = 0;      // distinct keywords, with or without a translation
    int untranslated = 0;  // ...of them without one
    int malformed = 0;     // rows that couldn't be read; skipped
    int firstMalformedLine = 0;
    int duplicates = 0;    // rows repeating a keyword seen before
    std::vector<WString> conflicts;  // keywords listed with two different translations
    int undecodable = 0;   // characters shown as U+FFFD
};

inline WString TrimSpaces(const WString& s)
{
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == L' ' || s[b] == L'\t'))
        ++b;
    while (e > b && (s[e - 1] == L' ' || s[e - 1] == L'\t'))
        --e;
    return s.substr(b, e - b);
}

// Parses the whole file into *t. Returns false if it doesn't look like a
// translation file (no keyword rows, or mostly unreadable ones), so a wrong
// file picked by mistake isn't taken as "no translations for anything".
//
// Duplicates: the first non-empty translation of a keyword wins; a later row
// with a different one is listed in t->conflicts. Leading and trailing
// spaces around a field are dropped; the text in between is kept exactly
// (bracketed notes, units, formulas).
inline bool ParseTranslations(const char* data, size_t size, TranslationTable* t)
{
    *t = TranslationTable();
    WString text = DecodeText(data, size, &t->encoding, &t->undecodable);
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == 0)
            return false;  // binary, or UTF-16 without a byte order mark
    }
    std::map<WString, bool> seen;  // keyword -> has a usable translation
    std::vector<WString> fields;
    size_t pos = 0;
    int line = 0;
    int rows = 0;
    while (pos < text.size()) {
        int startLine = ++line;
        bool ok = ReadCsvRecord(text, &pos, &fields);
        if (ok && fields.size() == 1 && TrimSpaces(fields[0]).empty())
            continue;  // blank line
        ++rows;
        WString key = fields.empty() ? WString() : TrimSpaces(fields[0]);
        if (!ok || fields.size() != 2 || key.empty()) {
            ++t->malformed;
            if (!t->firstMalformedLine)
                t->firstMalformedLine = startLine;
            continue;
        }
        WString value = TrimSpaces(fields[1]);
        if (key == L"CONTRIBUTORS") {
            t->contributors = value;
            continue;
        }
        if (key == L"LASTMODIFIED") {
            t->lastModified = value;
            continue;
        }
        std::map<WString, bool>::iterator s = seen.find(key);
        if (s == seen.end()) {
            ++t->keywords;
            seen[key] = !value.empty();
            if (!value.empty())
                t->entries[key] = value;
            continue;
        }
        ++t->duplicates;
        if (value.empty())
            continue;
        if (!s->second) {
            s->second = true;  // an earlier row had no translation: this one fills it
            t->entries[key] = value;
        } else if (t->entries[key] != value) {
            t->conflicts.push_back(key);
        }
    }
    t->untranslated = t->keywords - (int)t->entries.size();
    // A file that isn't a translation list: nothing usable, or more bad rows than good.
    return t->keywords > 0 && t->malformed <= rows / 2;
}

// The translation of a keyword, exactly as spelled; NULL if there is none.
inline const WString* LookupTranslation(const TranslationTable& t, const WString& keyword)
{
    std::map<WString, WString>::const_iterator i = t.entries.find(keyword);
    return i == t.entries.end() ? NULL : &i->second;
}

// Lower case for searching: ASCII and Latin-1 letters (the keywords are
// ASCII, translations English with the odd accented letter).
inline wchar_t FoldCase(wchar_t c)
{
    if ((c >= L'A' && c <= L'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7))
        return (wchar_t)(c + 32);
    return c;
}

inline WString FoldString(const WString& s)
{
    WString out(s);
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = FoldCase(out[i]);
    return out;
}
