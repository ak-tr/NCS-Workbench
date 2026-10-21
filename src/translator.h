// English names for coding keywords, from the user's own NCS Dummy
// Translations.csv (format: translations.h). Loaded once, shared by every
// window; applied only when drawing. Keywords themselves are never
// replaced: comparisons, files and sessions keep the BMW names.
//
// Settings: TranslationsPath (a file the user picked; "" = none),
// TranslationsAuto ("0" turns off looking next to NcsDummyPath),
// TranslationsShow ("0" hides them), TranslationsOrder ("Translation" puts
// the English name above the keyword; default keyword first).

#pragma once

#include <windows.h>

#include <vector>

#include "translations.h"
#include "xpstd.h"

enum TranslationSource {
    TSOURCE_NONE,       // nothing to load
    TSOURCE_FILE,       // the file in TranslationsPath
    TSOURCE_NCS_DUMMY,  // Translations.csv next to NcsDummy.exe
};

struct TranslationConfig {
    WString file;       // TranslationsPath
    bool autoFind = true;
    WString dummyExe;   // NcsDummyPath
};

struct TranslationState {
    TranslationSource source = TSOURCE_NONE;
    WString path;      // the file loaded or looked for; "" if none
    bool loaded = false;
    WString problem;   // why nothing is loaded; "" when loaded
    TranslationTable table;
};

// Translations.csv in NcsDummy.exe's folder, matched in any case. "" if none.
WString FindDummyTranslations(const WString& dummyExe);

// A picked file wins over the one next to NCS Dummy; if it can't be read,
// that is reported rather than quietly using another file.
void LoadTranslationsWith(const TranslationConfig& config, TranslationState* out);

// The app's translations, from the settings. LoadTranslations at startup
// and whenever a setting or the NCS Dummy path changed; nothing fails if
// there is no file, keywords just show untranslated.
void LoadTranslations();
const TranslationState& Translations();
// Changes whenever the translations or how they're shown change, so
// windows can tell their cached text is stale.
unsigned TranslationsGeneration();

bool TranslationsShown();  // loaded and not turned off
bool TranslationFirst();
void SetTranslationsShown(bool shown);
void SetTranslationFirst(bool first);

// A keyword's translation; NULL if hidden or there is none (or it's empty).
const WString* Translate(const String& keyword);
// A value cell's translation: only for a single value.
const WString* TranslateValues(const std::vector<String>& values);
