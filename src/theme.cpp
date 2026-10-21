// Mosaic theme for Win32. See theme.h.

#include <windows.h>
#include <commctrl.h>

#include "resource.h"
#include "theme.h"

Fonts g_fonts;
HBRUSH g_fieldBrush;

COLORREF kPage, kTile1, kTile2, kTile3, kPanel, kRule, kBorder, kNumber;
COLORREF kWhite, kInvert, kPressed;
COLORREF kText, kSecondary, kMuted, kFaint, kDisabled;
COLORREF kAccent, kSelect, kGood, kWarn, kBad, kBadHot, kBadPressed;

static int g_dpi = 96;
static HMODULE g_dwmapi, g_uxtheme;  // loaded in InitTheme
static HANDLE g_fontHandles[4];
static int g_theme = THEME_DAKOTA_BLACK;

struct ButtonInfo {
    WNDPROC proc;
    ButtonKind kind;
    const COLORREF* surface;
    bool hot;
    bool checked;  // BUTTON_CHECK
};

// ---------------------------------------------------------------------------
// Colour schemes

struct Palette {
    const wchar_t* name;
    bool light;
    // Same order as the variables in theme.h.
    COLORREF page, tile1, tile2, tile3, panel, rule, border, number;
    COLORREF white, invert, pressed;
    COLORREF text, secondary, muted, faint, disabled;
    COLORREF accent, select, good, warn, bad, badHot, badPressed;
};

// Text colours keep at least 4.5:1 against the darkest tile, except
// disabled (and Dakota Black's faint, the original Mosaic grey).
static const Palette kPalettes[THEME_COUNT] = {
    { L"Dakota Black", false,
      RGB(0x00, 0x00, 0x00), RGB(0x11, 0x11, 0x11), RGB(0x16, 0x16, 0x16), RGB(0x0c, 0x0c, 0x0c),
      RGB(0x08, 0x08, 0x08), RGB(0x26, 0x26, 0x26), RGB(0x40, 0x40, 0x40), RGB(0x2a, 0x2a, 0x2a),
      RGB(0xff, 0xff, 0xff), RGB(0xf2, 0xf2, 0xf2), RGB(0xd4, 0xd4, 0xd4),
      RGB(0xe5, 0xe5, 0xe5), RGB(0xa3, 0xa3, 0xa3), RGB(0x8a, 0x8a, 0x8a), RGB(0x73, 0x73, 0x73),
      RGB(0x52, 0x52, 0x52),
      RGB(0x4c, 0x8d, 0xff), RGB(0x12, 0x2b, 0x52), RGB(0x3d, 0xd6, 0x8c), RGB(0xff, 0xb2, 0x24),
      RGB(0xff, 0x6b, 0x6b), RGB(0xff, 0x8a, 0x8a), RGB(0xe0, 0x55, 0x55) },
    // BMW Individual headliner: neutral charcoal.
    { L"Anthracite", false,
      RGB(0x1c, 0x1d, 0x1f), RGB(0x25, 0x26, 0x2a), RGB(0x2a, 0x2b, 0x2f), RGB(0x22, 0x23, 0x26),
      RGB(0x1f, 0x20, 0x22), RGB(0x36, 0x37, 0x3b), RGB(0x50, 0x51, 0x57), RGB(0x3a, 0x3b, 0x40),
      RGB(0xff, 0xff, 0xff), RGB(0xf0, 0xf0, 0xf0), RGB(0xd0, 0xd0, 0xd2),
      RGB(0xe6, 0xe6, 0xe6), RGB(0xb0, 0xb0, 0xb2), RGB(0xa3, 0xa3, 0xa7), RGB(0x96, 0x96, 0x9a),
      RGB(0x5e, 0x5f, 0x63),
      RGB(0x6a, 0xa0, 0xff), RGB(0x23, 0x40, 0x6e), RGB(0x45, 0xd8, 0x92), RGB(0xff, 0xb8, 0x3a),
      RGB(0xff, 0x7a, 0x7a), RGB(0xff, 0x9a, 0x9a), RGB(0xe8, 0x64, 0x64) },
    // Dakota/Vernasca Mocha: warm brown-black.
    { L"Mocha", false,
      RGB(0x21, 0x1d, 0x1a), RGB(0x2a, 0x26, 0x22), RGB(0x30, 0x2b, 0x27), RGB(0x26, 0x22, 0x1e),
      RGB(0x24, 0x1f, 0x1c), RGB(0x3a, 0x34, 0x30), RGB(0x57, 0x50, 0x4a), RGB(0x40, 0x39, 0x33),
      RGB(0xff, 0xfb, 0xf6), RGB(0xef, 0xe9, 0xe1), RGB(0xd2, 0xcb, 0xc2),
      RGB(0xea, 0xe4, 0xdd), RGB(0xb8, 0xaf, 0xa5), RGB(0xaa, 0xa1, 0x97), RGB(0x9e, 0x94, 0x89),
      RGB(0x62, 0x5a, 0x53),
      RGB(0x6a, 0xa0, 0xff), RGB(0x2a, 0x3f, 0x63), RGB(0x45, 0xd8, 0x92), RGB(0xff, 0xb8, 0x3a),
      RGB(0xff, 0x7a, 0x7a), RGB(0xff, 0x9a, 0x9a), RGB(0xe8, 0x64, 0x64) },
    // Vernasca Canberra Beige: warm sand. Tiles are darker than the page.
    { L"Canberra Beige", true,
      RGB(0xf1, 0xe9, 0xda), RGB(0xe7, 0xdd, 0xcb), RGB(0xe2, 0xd7, 0xc3), RGB(0xeb, 0xe2, 0xd1),
      RGB(0xed, 0xe4, 0xd4), RGB(0xd8, 0xcc, 0xb5), RGB(0xb8, 0xa9, 0x8f), RGB(0xd2, 0xc5, 0xac),
      RGB(0x12, 0x0e, 0x08), RGB(0x23, 0x1d, 0x14), RGB(0x4d, 0x43, 0x34),
      RGB(0x23, 0x1d, 0x14), RGB(0x4d, 0x43, 0x34), RGB(0x5a, 0x4f, 0x3f), RGB(0x62, 0x56, 0x45),
      RGB(0xa8, 0x9c, 0x86),
      RGB(0x1a, 0x50, 0xc0), RGB(0xd3, 0xdc, 0xea), RGB(0x11, 0x6b, 0x3f), RGB(0x7a, 0x4c, 0x00),
      RGB(0xa8, 0x20, 0x20), RGB(0x8e, 0x1a, 0x1a), RGB(0x6e, 0x12, 0x12) },
    // M Merino Silverstone: plain light grey.
    { L"Silverstone", true,
      RGB(0xee, 0xee, 0xec), RGB(0xe3, 0xe3, 0xe1), RGB(0xde, 0xde, 0xdc), RGB(0xe6, 0xe6, 0xe4),
      RGB(0xe9, 0xe9, 0xe7), RGB(0xd2, 0xd2, 0xcf), RGB(0xab, 0xab, 0xa7), RGB(0xcc, 0xcc, 0xc9),
      RGB(0x0a, 0x0a, 0x0a), RGB(0x1a, 0x1a, 0x1a), RGB(0x44, 0x44, 0x44),
      RGB(0x1a, 0x1a, 0x1a), RGB(0x44, 0x44, 0x44), RGB(0x52, 0x52, 0x52), RGB(0x5c, 0x5c, 0x5c),
      RGB(0xa3, 0xa3, 0xa0),
      RGB(0x1a, 0x50, 0xc0), RGB(0xd3, 0xdc, 0xec), RGB(0x11, 0x6b, 0x3f), RGB(0x7a, 0x4c, 0x00),
      RGB(0xa8, 0x20, 0x20), RGB(0x8e, 0x1a, 0x1a), RGB(0x6e, 0x12, 0x12) },
};

const wchar_t* ThemeName(int theme)
{
    return kPalettes[theme >= 0 && theme < THEME_COUNT ? theme : 0].name;
}

int ThemeFromName(const wchar_t* name)
{
    for (int i = 0; i < THEME_COUNT; ++i)
        if (lstrcmpiW(name, kPalettes[i].name) == 0)
            return i;
    return THEME_DAKOTA_BLACK;
}

int CurrentTheme()
{
    return g_theme;
}

bool IsLightTheme()
{
    return kPalettes[g_theme].light;
}

void SetTheme(int theme)
{
    g_theme = theme >= 0 && theme < THEME_COUNT ? theme : THEME_DAKOTA_BLACK;
    const Palette& p = kPalettes[g_theme];
    kPage = p.page, kTile1 = p.tile1, kTile2 = p.tile2, kTile3 = p.tile3;
    kPanel = p.panel, kRule = p.rule, kBorder = p.border, kNumber = p.number;
    kWhite = p.white, kInvert = p.invert, kPressed = p.pressed;
    kText = p.text, kSecondary = p.secondary, kMuted = p.muted, kFaint = p.faint, kDisabled = p.disabled;
    kAccent = p.accent, kSelect = p.select, kGood = p.good, kWarn = p.warn;
    kBad = p.bad, kBadHot = p.badHot, kBadPressed = p.badPressed;
    if (g_fieldBrush)
        DeleteObject(g_fieldBrush);
    g_fieldBrush = CreateSolidBrush(kTile1);
}

int S(int px)
{
    return MulDiv(px, g_dpi, 96);
}

int Tracking()
{
    return S(1);
}

// Full path so a DLL of the same name next to the exe (USB stick) can't be picked up.
static HMODULE LoadSystemDll(const wchar_t* name)
{
    wchar_t path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (n == 0 || n + lstrlenW(name) + 2 > MAX_PATH)
        return NULL;
    lstrcatW(path, L"\\");
    lstrcatW(path, name);
    return LoadLibraryW(path);
}

template <class Fn>
static Fn GetProc(HMODULE module, const char* name)
{
    return module ? (Fn)(void*)GetProcAddress(module, name) : NULL;
}

static bool LoadFontResource(int id, int slot)
{
    HRSRC res = FindResourceW(NULL, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!res)
        return false;
    HGLOBAL data = LoadResource(NULL, res);
    DWORD count = 0;
    g_fontHandles[slot] = AddFontMemResourceEx(LockResource(data), SizeofResource(NULL, res), NULL, &count);
    return g_fontHandles[slot] != NULL;
}

static HFONT MakeFont(const wchar_t* face, int px, int weight)
{
    return CreateFontW(-S(px), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

void InitTheme()
{
    // Vista+: render at native DPI instead of being bitmap-stretched.
    typedef BOOL(WINAPI * SetProcessDPIAwareFn)();
    SetProcessDPIAwareFn setAware =
        GetProc<SetProcessDPIAwareFn>(GetModuleHandleW(L"user32.dll"), "SetProcessDPIAware");
    if (setAware)
        setAware();
    HDC screen = GetDC(NULL);
    g_dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(NULL, screen);

    // Static (non-variable) TTFs: GDI before Windows 10 can't use variable fonts.
    bool onest = LoadFontResource(IDR_FONT_ONEST, 0) && LoadFontResource(IDR_FONT_ONEST_MEDIUM, 1);
    bool mono = LoadFontResource(IDR_FONT_GEIST_MONO, 2) &&
                LoadFontResource(IDR_FONT_GEIST_MONO_MEDIUM, 3);
    const wchar_t* sans = onest ? L"Onest" : L"Tahoma";
    const wchar_t* sansMedium = onest ? L"Onest Medium" : L"Tahoma";
    const wchar_t* monoFace = mono ? L"Geist Mono" : L"Lucida Console";
    const wchar_t* monoMedium = mono ? L"Geist Mono Medium" : L"Lucida Console";
    int mediumWeight = onest ? FW_NORMAL : FW_BOLD;

    g_fonts.label = MakeFont(monoFace, 11, FW_NORMAL);
    g_fonts.button = MakeFont(monoMedium, 12, mono ? FW_NORMAL : FW_BOLD);
    g_fonts.mono = MakeFont(monoFace, 13, FW_NORMAL);
    g_fonts.number = MakeFont(monoFace, 44, FW_NORMAL);
    g_fonts.title = MakeFont(sansMedium, 18, mediumWeight);
    g_fonts.body = MakeFont(sans, 13, FW_NORMAL);
    g_fonts.heading = MakeFont(sansMedium, 22, mediumWeight);
    g_fonts.monoTitle = MakeFont(monoMedium, 18, mono ? FW_NORMAL : FW_BOLD);

    SetTheme(g_theme);  // also creates g_fieldBrush
    g_dwmapi = LoadSystemDll(L"dwmapi.dll");  // Vista+, absent on XP
    g_uxtheme = LoadSystemDll(L"uxtheme.dll");
}

void ShutdownTheme()
{
    HFONT* fonts[] = { &g_fonts.label, &g_fonts.button, &g_fonts.mono, &g_fonts.number,
                       &g_fonts.title, &g_fonts.body, &g_fonts.heading, &g_fonts.monoTitle };
    for (HFONT* f : fonts) {
        DeleteObject(*f);
        *f = NULL;
    }
    DeleteObject(g_fieldBrush);
    g_fieldBrush = NULL;
    for (HANDLE& h : g_fontHandles) {
        if (h)
            RemoveFontMemResourceEx(h);
        h = NULL;
    }
}

// ---------------------------------------------------------------------------
// Drawing

void Fill(HDC dc, const RECT& rc, COLORREF color)
{
    SetDCBrushColor(dc, color);
    FillRect(dc, &rc, (HBRUSH)GetStockObject(DC_BRUSH));
}

void Frame(HDC dc, const RECT& rc, COLORREF color, int width)
{
    RECT edges[4] = {
        { rc.left, rc.top, rc.right, rc.top + width },
        { rc.left, rc.bottom - width, rc.right, rc.bottom },
        { rc.left, rc.top, rc.left + width, rc.bottom },
        { rc.right - width, rc.top, rc.right, rc.bottom },
    };
    for (const RECT& e : edges)
        Fill(dc, e, color);
}

void DashedFrame(HDC dc, const RECT& rc, COLORREF color)
{
    int dash = S(4), step = S(4) + S(3);
    for (int x = rc.left; x < rc.right; x += step) {
        int x1 = x + dash < rc.right ? x + dash : rc.right;
        RECT top = { x, rc.top, x1, rc.top + 1 };
        RECT bottom = { x, rc.bottom - 1, x1, rc.bottom };
        Fill(dc, top, color);
        Fill(dc, bottom, color);
    }
    for (int y = rc.top; y < rc.bottom; y += step) {
        int y1 = y + dash < rc.bottom ? y + dash : rc.bottom;
        RECT left = { rc.left, y, rc.left + 1, y1 };
        RECT right = { rc.right - 1, y, rc.right, y1 };
        Fill(dc, left, color);
        Fill(dc, right, color);
    }
}

// Width of text in the DC's font and letter spacing, without the spacing after the last letter.
static int TextWidth(HDC dc, const wchar_t* text, int len, int tracking)
{
    SIZE size = {};
    GetTextExtentPoint32W(dc, text, len, &size);
    return len ? size.cx - tracking : 0;
}

int MeasureLabel(HDC dc, HFONT font, const wchar_t* text, int tracking)
{
    HGDIOBJ oldFont = SelectObject(dc, font);
    int oldExtra = SetTextCharacterExtra(dc, tracking);
    int width = TextWidth(dc, text, lstrlenW(text), tracking);
    SetTextCharacterExtra(dc, oldExtra);
    SelectObject(dc, oldFont);
    return width;
}

void DrawLabel(HDC dc, HFONT font, COLORREF color, const wchar_t* text, RECT rc, UINT flags, int tracking)
{
    HGDIOBJ oldFont = SelectObject(dc, font);
    int oldExtra = SetTextCharacterExtra(dc, tracking);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    bool singleLine = !(flags & DT_WORDBREAK);
    if (singleLine)
        flags |= DT_SINGLELINE | ((flags & DT_PATH_ELLIPSIS) ? 0 : DT_END_ELLIPSIS);

    // DrawText measures without the letter spacing: it neither ellipsizes nor
    // aligns tracked text correctly, so do both here.
    wchar_t shortened[512];
    if (tracking && singleLine) {
        int avail = rc.right - rc.left;
        int len = lstrlenW(text);
        int width = TextWidth(dc, text, len, tracking);
        if (width > avail && (flags & DT_PATH_ELLIPSIS)) {
            tracking = 0;  // let DrawText shorten the path, untracked
            SetTextCharacterExtra(dc, 0);
        } else if (width > avail) {
            int n = len < 500 ? len : 500;
            for (; n > 0; --n) {
                lstrcpynW(shortened, text, n + 1);
                lstrcatW(shortened, L"\x2026");
                if (TextWidth(dc, shortened, n + 1, tracking) <= avail)
                    break;
            }
            if (n == 0)
                shortened[0] = 0;
            text = shortened;
            width = TextWidth(dc, text, lstrlenW(text), tracking);
        }
        if (tracking && (flags & (DT_RIGHT | DT_CENTER)) && width <= avail) {
            rc.left += (flags & DT_RIGHT) ? avail - width : (avail - width) / 2;
            rc.right = rc.left + width + tracking;
            flags &= ~(DT_RIGHT | DT_CENTER);
        }
    }
    DrawTextW(dc, text, -1, &rc, flags | DT_NOPREFIX);
    SetTextCharacterExtra(dc, oldExtra);
    SelectObject(dc, oldFont);
}

void PaintBuffered(HWND hwnd, void (*paint)(HDC dc, const RECT& client))
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);
    paint(mem, rc);
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

// ---------------------------------------------------------------------------
// Buttons

static LRESULT CALLBACK ButtonProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    ButtonInfo* info = (ButtonInfo*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    WNDPROC proc = info->proc;
    switch (msg) {
    case WM_MOUSEMOVE:
        if (!info->hot) {
            info->hot = true;
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        break;
    case WM_MOUSELEAVE:
        info->hot = false;
        InvalidateRect(hwnd, NULL, FALSE);
        break;
    case WM_LBUTTONDBLCLK:
        msg = WM_LBUTTONDOWN;  // owner-drawn buttons eat fast second clicks otherwise
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_SETCURSOR:
        if (IsWindowEnabled(hwnd)) {  // every clickable themed button gets the hand
            SetCursor(LoadCursorW(NULL, IDC_HAND));
            return TRUE;
        }
        break;
    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        delete info;
        break;
    }
    return CallWindowProcW(proc, hwnd, msg, wParam, lParam);
}

HWND CreateThemedButton(HWND parent, int id, const wchar_t* text, ButtonKind kind, const COLORREF& surface)
{
    HWND h = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                             0, 0, 0, 0, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    ButtonInfo* info = new ButtonInfo;
    info->kind = kind;
    info->surface = &surface;
    info->hot = false;
    info->checked = false;
    info->proc =(WNDPROC)GetWindowLongPtrW(h, GWLP_WNDPROC);
    SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)info);
    SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)ButtonProc);
    SendMessageW(h, WM_SETFONT, (WPARAM)g_fonts.button, FALSE);
    return h;
}

void DrawThemedButton(const DRAWITEMSTRUCT* dis)
{
    ButtonInfo* info = (ButtonInfo*)GetWindowLongPtrW(dis->hwndItem, GWLP_USERDATA);
    if (!info)
        return;
    HDC dc = dis->hDC;
    RECT rc = dis->rcItem;
    bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    bool focused = (dis->itemState & ODS_FOCUS) && !(dis->itemState & ODS_NOFOCUSRECT);
    bool hot = info->hot && !disabled;
    wchar_t text[160];
    GetWindowTextW(dis->hwndItem, text, 160);

    if (info->kind == BUTTON_CHECK) {
        Fill(dc, rc, *info->surface);
        int box = S(14), cy = (rc.top + rc.bottom) / 2;
        RECT b = { rc.left + S(2), cy - box / 2, rc.left + S(2) + box, cy - box / 2 + box };
        if (info->checked && !disabled) {
            Frame(dc, b, kInvert);
            RECT dot = { b.left + S(4), b.top + S(4), b.right - S(4), b.bottom - S(4) };
            Fill(dc, dot, kInvert);
        } else {
            Fill(dc, b, kPage);
            Frame(dc, b, disabled ? kRule : hot ? kWhite : kMuted);
            if (info->checked) {
                RECT dot = { b.left + S(4), b.top + S(4), b.right - S(4), b.bottom - S(4) };
                Fill(dc, dot, kDisabled);
            }
        }
        RECT tr = { b.right + S(12), rc.top, rc.right - S(4), rc.bottom };
        DrawLabel(dc, g_fonts.body, disabled ? kDisabled : hot || info->checked ? kWhite : kText, text, tr,
                  DT_LEFT | DT_VCENTER);
        if (focused)
            Frame(dc, rc, kAccent);
        return;
    }

    if (info->kind == BUTTON_LINK) {
        Fill(dc, rc, *info->surface);
        HGDIOBJ oldFont = SelectObject(dc, g_fonts.button);
        int oldExtra = SetTextCharacterExtra(dc, Tracking());
        SIZE size;
        GetTextExtentPoint32W(dc, text, lstrlenW(text), &size);
        SetTextCharacterExtra(dc, oldExtra);
        SelectObject(dc, oldFont);

        RECT tr = rc;
        tr.left = rc.right - size.cx;
        COLORREF fg = disabled ? kDisabled : hot ? kWhite : kText;
        DrawLabel(dc, g_fonts.button, fg, text, tr, DT_LEFT | DT_VCENTER, Tracking());
        int y = (rc.top + rc.bottom + size.cy) / 2;
        RECT underline = { tr.left, y, rc.right - Tracking(), y + 1 };
        Fill(dc, underline, hot ? kWhite : kDisabled);
        if (focused)
            Frame(dc, rc, kAccent);
        return;
    }

    COLORREF bg, fg, border;
    if (disabled) {
        bg = *info->surface;
        fg = kDisabled;
        border = kRule;
    } else if (info->kind == BUTTON_PRIMARY) {
        bg = pressed ? kPressed : hot ? kWhite : kInvert;
        fg = kPage;
        border = bg;
    } else if (info->kind == BUTTON_DANGER) {
        bg = pressed ? kBadPressed : hot ? kBadHot : kBad;
        fg = kPage;
        border = bg;
    } else {
        bool inverted = hot || pressed;  // Mosaic hover: invert
        bg = pressed ? kPressed : inverted ? kInvert : kTile1;
        fg = inverted ? kPage : kWhite;
        border = inverted ? bg : kBorder;
    }
    Fill(dc, rc, bg);
    Frame(dc, rc, border);
    DrawLabel(dc, g_fonts.button, fg, text, rc, DT_CENTER | DT_VCENTER, Tracking());
    if (focused)
        Frame(dc, rc, kAccent, S(2));
}

void SetThemedCheck(HWND button, bool checked)
{
    ButtonInfo* info = (ButtonInfo*)GetWindowLongPtrW(button, GWLP_USERDATA);
    if (info && info->checked != checked) {
        info->checked = checked;
        InvalidateRect(button, NULL, FALSE);
    }
}

int LinkWidth(HWND link)
{
    wchar_t text[64];
    GetWindowTextW(link, text, 64);
    HDC dc = GetDC(link);
    int w = MeasureLabel(dc, g_fonts.button, text, Tracking());
    ReleaseDC(link, dc);
    return w + Tracking() + S(2);
}

RECT RightLink(HWND link, int right, int cy)
{
    RECT r = { right - LinkWidth(link), cy - S(12), right, cy + S(12) };
    return r;
}

HWND CreateThemedList(HWND parent, int id, COLORREF background, int rowHeight, HIMAGELIST* rowImages)
{
    HWND list = CreateWindowExW(0, WC_LISTVIEWW, L"",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_NOCOLUMNHEADER | LVS_SINGLESEL |
                                    LVS_SHOWSELALWAYS | LVS_OWNERDATA,
                                0, 0, 0, 0, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    ListView_SetBkColor(list, background);
    SendMessageW(list, WM_SETFONT, (WPARAM)g_fonts.body, FALSE);
    *rowImages = ImageList_Create(1, rowHeight, ILC_COLOR32, 1, 0);
    ListView_SetImageList(list, *rowImages, LVSIL_SMALL);
    LVCOLUMNW col = {};
    col.mask = LVCF_WIDTH;
    col.cx = S(400);
    ListView_InsertColumn(list, 0, &col);
    ApplyThemedControl(list, L"DarkMode_Explorer");
    return list;
}

bool IsThemedChecked(HWND button)
{
    ButtonInfo* info = (ButtonInfo*)GetWindowLongPtrW(button, GWLP_USERDATA);
    return info && info->checked;
}

// ---------------------------------------------------------------------------
// Windows 10/11 frame and controls

void ApplyThemedFrame(HWND hwnd)
{
    typedef HRESULT(WINAPI * SetAttributeFn)(HWND, DWORD, LPCVOID, DWORD);
    SetAttributeFn set = GetProc<SetAttributeFn>(g_dwmapi, "DwmSetWindowAttribute");
    if (!set)
        return;
    BOOL dark = !IsLightTheme();
    // DWMWA_USE_IMMERSIVE_DARK_MODE is 20, or 19 before Windows 10 20H1.
    if (FAILED(set(hwnd, 20, &dark, sizeof(dark))))
        set(hwnd, 19, &dark, sizeof(dark));
    // Windows 11: caption, caption text and border colours.
    COLORREF caption = kPage, text = kText, border = kRule;
    set(hwnd, 35, &caption, sizeof(caption));
    set(hwnd, 36, &text, sizeof(text));
    set(hwnd, 34, &border, sizeof(border));
}

void ApplyThemedControl(HWND hwnd, const wchar_t* darkSubApp)
{
    typedef HRESULT(WINAPI * SetWindowThemeFn)(HWND, LPCWSTR, LPCWSTR);
    SetWindowThemeFn set = GetProc<SetWindowThemeFn>(g_uxtheme, "SetWindowTheme");
    // Unknown sub-app names fall back to the normal theme, as does NULL.
    if (set)
        set(hwnd, IsLightTheme() ? NULL : darkSubApp, NULL);
}
