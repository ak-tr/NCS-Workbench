// Tests for src/translations.h. Built and run by test.cmd.
//
// NCS Dummy's Translations.csv is its authors' work and isn't in the repo:
// these files are small, made-up ones in the same format.

#include <cassert>
#include <cstdio>
#include <cstring>

#include "../src/translations.h"

static TranslationTable Parse(const char* csv, bool* ok = NULL)
{
    TranslationTable t;
    bool r = ParseTranslations(csv, strlen(csv), &t);
    if (ok)
        *ok = r;
    else
        assert(r);
    return t;
}

static const WString& Get(const TranslationTable& t, const wchar_t* key)
{
    const WString* v = LookupTranslation(t, key);
    assert(v);
    return *v;
}

static void TestBasic()
{
    TranslationTable t = Parse("CONTRIBUTORS,\"revtor,IcemanBHE,joako\"\r\n"
                               "LASTMODIFIED,20201210\r\n"
                               "\r\n"
                               "aktiv,Enabled\r\n"
                               "nicht_aktiv,Not enabled\r\n"
                               "BC_DIGITAL_V,On-board computer (OBC) digital speed display\r\n"
                               "0_004nms/grad,\r\n");
    assert(t.contributors == L"revtor,IcemanBHE,joako");
    assert(t.lastModified == L"20201210");
    assert(t.keywords == 4);  // metadata isn't a keyword
    assert(t.entries.size() == 3);
    assert(t.untranslated == 1);
    assert(t.malformed == 0 && t.duplicates == 0);
    assert(Get(t, L"aktiv") == L"Enabled");
    assert(Get(t, L"BC_DIGITAL_V") == L"On-board computer (OBC) digital speed display");
    assert(!LookupTranslation(t, L"0_004nms/grad"));  // empty = no translation
    assert(!LookupTranslation(t, L"CONTRIBUTORS") && !LookupTranslation(t, L"LASTMODIFIED"));
    assert(!LookupTranslation(t, L"MISSING"));
    assert(t.encoding == TEXT_CP1252);
}

static void TestQuoting()
{
    TranslationTable t = Parse(
        "04bh,\"Check control notification \"\"Trailer tow bar electrics!\"\"\"\r\n"
        "BC_DIGITAL_V_KORREKTUR,\"On-board computer (OBC) digital speed display correction [aktiv: corrected speed, "
        "nicht_aktiv: true speed]\"\r\n"
        "alpine,\"Alpine-Sharp (manufacturer) 6.5\"\" fixed display\"\r\n"
        "\"QUOTED_KEY\",Quoted key\r\n"
        "AFTER,still read\r\n"
        "INCH,6.5\" display\r\n");  // a quote inside an unquoted field is kept
    assert(Get(t, L"04bh") == L"Check control notification \"Trailer tow bar electrics!\"");
    assert(Get(t, L"BC_DIGITAL_V_KORREKTUR") ==
           L"On-board computer (OBC) digital speed display correction [aktiv: corrected speed, nicht_aktiv: true "
           L"speed]");  // brackets, commas: kept exactly
    assert(Get(t, L"alpine") == L"Alpine-Sharp (manufacturer) 6.5\" fixed display");
    assert(Get(t, L"QUOTED_KEY") == L"Quoted key");
    assert(Get(t, L"AFTER") == L"still read");
    assert(Get(t, L"INCH") == L"6.5\" display");
    assert(t.malformed == 0);
}

static void TestLineEndings()
{
    TranslationTable lf = Parse("a,A\nb,B\n");
    assert(lf.entries.size() == 2 && Get(lf, L"b") == L"B");
    TranslationTable none = Parse("a,A\r\nb,B");  // no line break at the end
    assert(Get(none, L"b") == L"B");
    TranslationTable spaces = Parse("  padded ,  Padded value  \r\n");
    assert(Get(spaces, L"padded") == L"Padded value");
}

static void TestMalformed()
{
    TranslationTable t = Parse("good,Good\r\n"
                               "no_comma\r\n"
                               "three,fields,here\r\n"
                               ",no key\r\n"
                               "after_quote,\"x\"y\r\n"
                               "also_good,Also good\r\n"
                               "open,\"never closed\r\n"
                               "last,Last\r\n"
                               "quoted,\"Has \"\"quotes\"\"\"\r\n"
                               "two_lines,\"first\r\n"
                               "second\"\r\n"
                               "x1,X1\r\n"
                               "x2,X2\r\n"
                               "x3,X3\r\n"
                               "x4,X4\r\n");
    assert(t.malformed == 7);  // a quoted field can't span lines: both halves are bad rows
    assert(t.firstMalformedLine == 2);
    assert(Get(t, L"good") == L"Good");
    assert(Get(t, L"also_good") == L"Also good");
    assert(!LookupTranslation(t, L"three") && !LookupTranslation(t, L"open"));
    assert(!LookupTranslation(t, L"two_lines"));
    // The unclosed quote only costs its own line: the rows after it are read,
    // also ones with quotes of their own.
    assert(Get(t, L"last") == L"Last");
    assert(Get(t, L"quoted") == L"Has \"quotes\"");
    assert(Get(t, L"x1") == L"X1");
}

static void TestDuplicates()
{
    TranslationTable t = Parse("dup,First\r\n"
                               "dup,First\r\n"
                               "dup,Second\r\n"
                               "empty_then,\r\n"
                               "empty_then,Filled\r\n"
                               "Case,Upper\r\n"
                               "case,Lower\r\n");
    assert(Get(t, L"dup") == L"First");  // the first non-empty one wins
    assert(t.duplicates == 3);
    assert(t.conflicts.size() == 1 && t.conflicts[0] == L"dup");
    assert(Get(t, L"empty_then") == L"Filled");
    // Keywords keep their case: two different keywords.
    assert(Get(t, L"Case") == L"Upper" && Get(t, L"case") == L"Lower");
    assert(t.keywords == 4 && t.untranslated == 0);
}

static void TestEncodings()
{
    // Windows-1252: degree sign, e acute, euro, an undefined byte.
    const char cp1252[] = "GRAD,121 \xB0" "C\r\nCAFE,caf\xE9\r\nEURO,\x80 5\r\nODD,x\x81y\r\n";
    TranslationTable t = Parse(cp1252);
    assert(t.encoding == TEXT_CP1252);
    assert(Get(t, L"GRAD") == L"121 \x00B0" L"C");
    assert(Get(t, L"CAFE") == L"caf\x00E9");
    assert(Get(t, L"EURO") == L"\x20AC 5");
    assert(Get(t, L"ODD") == L"x\xFFFDy" && t.undecodable == 1);

    // UTF-8, with and without a byte order mark.
    const char utf8[] = "GRAD,121 \xC2\xB0" "C\r\n";
    TranslationTable u = Parse(utf8);
    assert(u.encoding == TEXT_UTF8 && Get(u, L"GRAD") == L"121 \x00B0" L"C");
    const char bom[] = "\xEF\xBB\xBF" "aktiv,Enabled\r\n";
    TranslationTable b = Parse(bom);
    assert(b.encoding == TEXT_UTF8 && Get(b, L"aktiv") == L"Enabled");

    // UTF-16 LE with a byte order mark.
    const char utf16[] = "\xFF\xFE" "a\0,\0\xB0\0\r\0\n\0";
    TranslationTable w;
    assert(ParseTranslations(utf16, sizeof(utf16) - 1, &w));
    assert(w.encoding == TEXT_UTF16LE && Get(w, L"a") == L"\x00B0");
}

static void TestInvalidFiles()
{
    bool ok = true;
    Parse("", &ok);
    assert(!ok);  // nothing in it
    Parse("CONTRIBUTORS,x\r\nLASTMODIFIED,1\r\n", &ok);
    assert(!ok);  // metadata only
    Parse("just some text\r\nwithout commas\r\nat all\r\n", &ok);
    assert(!ok);
    const char binary[] = "a,b\0\x01\x02";
    TranslationTable t;
    assert(!ParseTranslations(binary, sizeof(binary) - 1, &t));
    // Mostly fine with one bad row: still a translation file.
    Parse("a,A\r\nb,B\r\nbad\r\n", &ok);
    assert(ok);
}

static void TestFolding()
{
    assert(FoldString(L"BC_Digital \x00C9") == L"bc_digital \x00E9");
    assert(FoldCase(L'\x00D7') == L'\x00D7');  // the multiplication sign has no lower case
    assert(TrimSpaces(L"  a b \t") == L"a b");
}

int main()
{
    TestBasic();
    TestQuoting();
    TestLineEndings();
    TestMalformed();
    TestDuplicates();
    TestEncodings();
    TestInvalidFiles();
    TestFolding();
    printf("translations_test: all passed\n");
    return 0;
}
