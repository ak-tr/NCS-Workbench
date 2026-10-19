// Mosaic look (akif.kr DESIGN.md) for raw Win32: palette, embedded fonts,
// DPI scaling, owner-drawn buttons. Unlike the site, accent colours are
// allowed in this app (focus, selection, status).

#pragma once

#include <windows.h>

// Greys
const COLORREF kPage = RGB(0x00, 0x00, 0x00);
const COLORREF kTile1 = RGB(0x11, 0x11, 0x11);
const COLORREF kTile2 = RGB(0x16, 0x16, 0x16);
const COLORREF kTile3 = RGB(0x0c, 0x0c, 0x0c);
const COLORREF kPanel = RGB(0x08, 0x08, 0x08);
const COLORREF kRule = RGB(0x26, 0x26, 0x26);
const COLORREF kBorder = RGB(0x40, 0x40, 0x40);
const COLORREF kNumber = RGB(0x2a, 0x2a, 0x2a);
const COLORREF kWhite = RGB(0xff, 0xff, 0xff);
const COLORREF kInvert = RGB(0xf2, 0xf2, 0xf2);
const COLORREF kPressed = RGB(0xd4, 0xd4, 0xd4);
const COLORREF kText = RGB(0xe5, 0xe5, 0xe5);
const COLORREF kSecondary = RGB(0xa3, 0xa3, 0xa3);
const COLORREF kMuted = RGB(0x8a, 0x8a, 0x8a);
const COLORREF kFaint = RGB(0x73, 0x73, 0x73);
const COLORREF kDisabled = RGB(0x52, 0x52, 0x52);

// Accents
const COLORREF kAccent = RGB(0x4c, 0x8d, 0xff);  // keyboard focus
const COLORREF kSelect = RGB(0x12, 0x2b, 0x52);  // selected list row
const COLORREF kGood = RGB(0x3d, 0xd6, 0x8c);
const COLORREF kWarn = RGB(0xff, 0xb2, 0x24);
const COLORREF kBad = RGB(0xff, 0x6b, 0x6b);

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
HWND CreateThemedButton(HWND parent, int id, const wchar_t* text, ButtonKind kind, COLORREF surface);
void DrawThemedButton(const DRAWITEMSTRUCT* dis);
void SetThemedCheck(HWND button, bool checked);
bool IsThemedChecked(HWND button);

// Dark title bar on Windows 10 1809+ and 11; no effect on older systems.
void ApplyDarkFrame(HWND hwnd);
// Dark scrollbars/combo parts on Windows 10+ ("DarkMode_Explorer", "DarkMode_CFD").
void ApplyDarkControl(HWND hwnd, const wchar_t* subApp);

void Fill(HDC dc, const RECT& rc, COLORREF color);
void Frame(HDC dc, const RECT& rc, COLORREF color, int width = 1);
void DashedFrame(HDC dc, const RECT& rc, COLORREF color);
// Single-line text is shortened with an ellipsis to fit (also when tracked).
void DrawLabel(HDC dc, HFONT font, COLORREF color, const wchar_t* text, RECT rc, UINT flags,
               int tracking = 0);
int MeasureLabel(HDC dc, HFONT font, const wchar_t* text, int tracking = 0);
// WM_PAINT through an off-screen bitmap (no flicker).
void PaintBuffered(HWND hwnd, void (*paint)(HDC dc, const RECT& client));
