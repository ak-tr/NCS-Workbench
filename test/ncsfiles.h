// Synthetic SP-Daten files for the tests, built record by record in the
// format of src/ncsdata.h. SP-Daten is BMW's data and stays out of the repo.

#pragma once

#include <cstring>
#include <vector>

typedef std::vector<unsigned char> Bytes;

inline void Record(Bytes& out, unsigned type, const Bytes& payload)
{
    out.push_back((unsigned char)payload.size());
    out.push_back((unsigned char)(type & 0xFF));
    out.push_back((unsigned char)(type >> 8));
    out.insert(out.end(), payload.begin(), payload.end());
    out.push_back(0x5A);  // check byte, ignored
}

inline void Put(Bytes& b, const char* s)
{
    b.insert(b.end(), s, s + strlen(s) + 1);
}

inline void PutWord(Bytes& b, unsigned w)
{
    b.push_back((unsigned char)(w & 0xFF));
    b.push_back((unsigned char)(w >> 8));
}

// Header defining one table: id, name, format, field names.
inline void Table(Bytes& out, unsigned id, const char* name, const char* format, const char* fields)
{
    Bytes p;
    PutWord(p, id);
    Put(p, name);
    Record(out, 0x0300, p);
    p.clear();
    Put(p, format);
    Record(out, 0x0400, p);
    p.clear();
    Put(p, fields);
    Record(out, 0x0500, p);
}

inline void EndHeader(Bytes& out)
{
    Record(out, 0xFF00, Bytes());
}

// SWTFSW / SWTPSW: table SWT_EINTRAG, "WS" = id, name.
inline Bytes KeywordFile(const char* const* names, const unsigned* ids, int count)
{
    Bytes f;
    Table(f, 0, "DATEINAME", "S", "NAME");
    Table(f, 1, "SWT_EINTRAG", "WS", "KEYID,KEYWORD");
    EndHeader(f);
    for (int i = 0; i < count; ++i) {
        Bytes p;
        PutWord(p, ids[i]);
        Put(p, names[i]);
        Record(f, 1, p);
    }
    f.push_back(0);  // trailing bytes are ignored
    return f;
}

// PARZUWEISUNG_FSW / _DIR record: {L} count + longs, L, W, then the function id.
inline Bytes FswRecord(unsigned id, int longs)
{
    Bytes p;
    p.push_back((unsigned char)longs);
    for (int i = 0; i < longs * 4 + 4 + 2; ++i)
        p.push_back(0);
    PutWord(p, id);
    p.push_back(0x01);  // INDEX etc.
    return p;
}

// PARZUWEISUNG_PSW1 record: value id, then data.
inline Bytes PswRecord(unsigned id)
{
    Bytes p;
    PutWord(p, id);
    p.push_back(0x01);
    p.push_back(0x00);
    p.push_back(0x00);
    return p;
}

// A module file in the real layout: table ids as in the E89 files.
struct ModuleBuilder {
    Bytes f;
    explicit ModuleBuilder(int codingIndex)
    {
        Table(f, 0, "DATEINAME", "S", "NAME");
        Table(f, 1, "SGID_CODIERINDEX", "B(B)", "WERT,WERT2");
        Table(f, 15, "PARZUWEISUNG_PSW2", "(B)", "DATUM");
        Table(f, 16, "PARZUWEISUNG_PSW1", "W(B)", "PSW,DATUM");
        Table(f, 17, "PARZUWEISUNG_DIR", "{L}LWW{B}(B)(A)B", "BLOCKNR,WORTADR,BYTEADR,FSW,INDEX,MASKE,OPERATION,EINHEIT");
        Table(f, 18, "PARZUWEISUNG_FSW", "{L}LWW{B}(B){B}{B}", "BLOCKNR,WORTADR,BYTEADR,FSW,INDEX,MASKE,EINHEIT,INDIVID");
        EndHeader(f);
        Bytes name;
        Put(name, "TEST.C00");
        Record(f, 0, name);
        if (codingIndex >= 0) {
            Bytes idx;
            idx.push_back((unsigned char)codingIndex);
            idx.push_back(0);
            idx.push_back(0);
            Record(f, 1, idx);
        }
    }
    // A function with a list of values; psw2 adds a PSW2 record after the first value.
    ModuleBuilder& Function(unsigned id, const unsigned* values, int count, bool psw2 = false)
    {
        Record(f, 18, FswRecord(id, 1));
        for (int i = 0; i < count; ++i) {
            Record(f, 16, PswRecord(values[i]));
            if (psw2 && i == 0) {
                Bytes data;
                data.push_back(0x7F);
                Record(f, 15, data);
            }
        }
        return *this;
    }
    ModuleBuilder& Direct(unsigned id)
    {
        Record(f, 17, FswRecord(id, 2));
        return *this;
    }
};
