// Mosaic look (akif.kr DESIGN.md) for raw Win32: palette, embedded fonts,
// DPI scaling, owner-drawn buttons. Unlike the site, accent colours are
// allowed in this app (focus, selection, status).

#pragma once

#include <windows.h>

// Colour schemes, named after BMW interior colours. Dakota Black is the
// Mosaic look and the default; the others are for readability: two dim
// (not fully black) and two light. Setting "Theme" holds the name.
enum ThemeId { THEME_DAKOTA_BLACK, THEME_ANTHRACITE, THEME_MOCHA, THEME_CANBERRA_BEIGE, THEME_SILVERSTONE, THEME_COUNT };

const wchar_t* ThemeName(int theme);         // "Dakota Black"
int ThemeFromName(const wchar_t* name);       // unknown names give Dakota Black
// Sets the colours below. Windows already open need ApplyThemedFrame/Control
// and a repaint; themed buttons follow on their own.
void SetTheme(int theme);
int CurrentTheme();
bool IsLightTheme();

// The palette of the current scheme. Names are from the dark design: in a
// light scheme kPage is the lightest colour and kWhite the darkest text.
// Greys
extern COLORREF kPage, kTile1, kTile2, kTile3, kPanel, kRule, kBorder, kNumber;
extern COLORREF kWhite, kInvert, kPressed;  // strongest text; primary button fill, pressed
extern COLORREF kText, kSecondary, kMuted, kFaint, kDisabled;
// Accents
extern COLORREF kAccent;  // keyboard focus
extern COLORREF kSelect;  // selected list row
extern COLORREF kGood, kWarn, kBad;
extern COLORREF kBadHot, kBadPressed;  // BUTTON_DANGER hover, pressed

struct Fonts {
    HFONT label;    // Geist Mono 11, uppercase labels and meta
    HFONT button;   // Geist Mono Medium 12
    HFONT mono;     // Geist Mono 13, file names and inputs
    HFONT number;   // Geist Mono 44, big faint step numbers
    HFONT title;    // Onest Medium 18
    HFONT body;     // Onest 13
    HFONT heading;  // Onest Medium 22
};

extern Fonts g_fonts;
extern HBRUSH g_fieldBrush;  // input backgrounds (WM_CTLCOLOREDIT/LISTBOX)

// DANGER: red fill, for the confirm button of a destructive action.
// CHECK: square box + label (also used as a radio); the parent flips it on BN_CLICKED.
enum ButtonKind { BUTTON_PRIMARY, BUTTON_SECONDARY, BUTTON_DANGER, BUTTON_LINK, BUTTON_CHECK };

// Call once before creating windows: DPI awareness, fonts, brushes.
void InitTheme();
void ShutdownTheme();

// Scales a 96-dpi pixel value to the system DPI.
int S(int px);
// Letter spacing for mono labels (DESIGN.md: tracking-wide).
int Tracking();

// `surface` is the colour behind the button, used for links and disabled states.
// It must be one of the palette variables above: the button keeps its address,
// so it follows a theme change.
HWND CreateThemedButton(HWND parent, int id, const wchar_t* text, ButtonKind kind, const COLORREF& surface);
void DrawThemedButton(const DRAWITEMSTRUCT* dis);
void SetThemedCheck(HWND button, bool checked);
bool IsThemedChecked(HWND button);

// Title bar in the scheme's colours: dark on Windows 10 1809+ and 11 for the
// dark schemes, caption colours on Windows 11; no effect on older systems.
void ApplyThemedFrame(HWND hwnd);
// Dark scrollbars/combo parts on Windows 10+ ("DarkMode_Explorer", "DarkMode_CFD")
// for the dark schemes; the normal theme for the light ones.
void ApplyThemedControl(HWND hwnd, const wchar_t* darkSubApp);

void Fill(HDC dc, const RECT& rc, COLORREF color);
void Frame(HDC dc, const RECT& rc, COLORREF color, int width = 1);
void DashedFrame(HDC dc, const RECT& rc, COLORREF color);
// Single-line text is shortened with an ellipsis to fit (also when tracked).
void DrawLabel(HDC dc, HFONT font, COLORREF color, const wchar_t* text, RECT rc, UINT flags,
               int tracking = 0);
int MeasureLabel(HDC dc, HFONT font, const wchar_t* text, int tracking = 0);
// WM_PAINT through an off-screen bitmap (no flicker).
void PaintBuffered(HWND hwnd, void (*paint)(HDC dc, const RECT& client));
