// Mosaic theme for Win32. See theme.h.

#include <windows.h>

#include "resource.h"
#include "theme.h"

Fonts g_fonts;
HBRUSH g_fieldBrush;

static int g_dpi = 96;
static HMODULE g_dwmapi, g_uxtheme;  // loaded in InitTheme
static HANDLE g_fontHandles[4];

struct ButtonInfo {
    WNDPROC proc;
    ButtonKind kind;
    COLORREF surface;
    bool hot;
};

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

    g_fieldBrush = CreateSolidBrush(kTile1);
    g_dwmapi = LoadSystemDll(L"dwmapi.dll");  // Vista+, absent on XP
    g_uxtheme = LoadSystemDll(L"uxtheme.dll");
}

void ShutdownTheme()
{
    HFONT* fonts[] = { &g_fonts.label, &g_fonts.button, &g_fonts.mono, &g_fonts.number,
                       &g_fonts.title, &g_fonts.body, &g_fonts.heading };
    for (HFONT* f : fonts) {
        DeleteObject(*f);
        *f = NULL;
    }
    DeleteObject(g_fieldBrush);
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

HWND CreateThemedButton(HWND parent, int id, const wchar_t* text, ButtonKind kind, COLORREF surface)
{
    HWND h = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                             0, 0, 0, 0, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    ButtonInfo* info = new ButtonInfo;
    info->kind = kind;
    info->surface = surface;
    info->hot = false;
    info->proc = (WNDPROC)GetWindowLongPtrW(h, GWLP_WNDPROC);
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
    wchar_t text[64];
    GetWindowTextW(dis->hwndItem, text, 64);

    if (info->kind == BUTTON_LINK) {
        Fill(dc, rc, info->surface);
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
        bg = info->surface;
        fg = kDisabled;
        border = kRule;
    } else if (info->kind == BUTTON_PRIMARY) {
        bg = pressed ? kPressed : hot ? kWhite : kInvert;
        fg = kPage;
        border = bg;
    } else if (info->kind == BUTTON_DANGER) {
        bg = pressed ? RGB(0xe0, 0x55, 0x55) : hot ? RGB(0xff, 0x8a, 0x8a) : kBad;
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

// ---------------------------------------------------------------------------
// Windows 10/11 dark frame and controls

void ApplyDarkFrame(HWND hwnd)
{
    typedef HRESULT(WINAPI * SetAttributeFn)(HWND, DWORD, LPCVOID, DWORD);
    SetAttributeFn set = GetProc<SetAttributeFn>(g_dwmapi, "DwmSetWindowAttribute");
    if (!set)
        return;
    BOOL dark = TRUE;
    // DWMWA_USE_IMMERSIVE_DARK_MODE is 20, or 19 before Windows 10 20H1.
    if (FAILED(set(hwnd, 20, &dark, sizeof(dark))))
        set(hwnd, 19, &dark, sizeof(dark));
    // Windows 11: caption, caption text and border colours.
    COLORREF caption = kPage, text = kText, border = kRule;
    set(hwnd, 35, &caption, sizeof(caption));
    set(hwnd, 36, &text, sizeof(text));
    set(hwnd, 34, &border, sizeof(border));
}

void ApplyDarkControl(HWND hwnd, const wchar_t* subApp)
{
    typedef HRESULT(WINAPI * SetWindowThemeFn)(HWND, LPCWSTR, LPCWSTR);
    SetWindowThemeFn set = GetProc<SetWindowThemeFn>(g_uxtheme, "SetWindowTheme");
    if (set)
        set(hwnd, subApp, NULL);  // unknown sub-app names fall back to the normal theme
}
