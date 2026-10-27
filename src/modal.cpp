// Themed modal popups. See modal.h.

#include <windows.h>

#include "modal.h"
#include "resource.h"
#include "theme.h"

static DWORD ModalStyle(bool resizable)
{
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    return resizable ? style | WS_THICKFRAME | WS_MAXIMIZEBOX : style;
}

SIZE ModalWindowSize(int clientWidth, int clientHeight, bool resizable)
{
    RECT rc = { 0, 0, clientWidth, clientHeight };
    AdjustWindowRectEx(&rc, ModalStyle(resizable), FALSE, WS_EX_DLGMODALFRAME);
    SIZE size = { rc.right - rc.left, rc.bottom - rc.top };
    return size;
}

HWND CreateModal(HWND owner, const wchar_t* className, WNDPROC proc, const wchar_t* title, int clientWidth,
                 int clientHeight, void* param, bool resizable)
{
    HINSTANCE inst = GetModuleHandleW(NULL);
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    if (!GetClassInfoExW(inst, className, &wc)) {
        wc.lpfnWndProc = proc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = className;
        if (!RegisterClassExW(&wc))
            return NULL;
    }

    DWORD style = ModalStyle(resizable);
    SIZE size = ModalWindowSize(clientWidth, clientHeight, resizable);
    int w = size.cx, h = size.cy;
    RECT o;
    GetWindowRect(owner, &o);
    int x = o.left + ((o.right - o.left) - w) / 2;
    int y = o.top + ((o.bottom - o.top) - h) / 2;

    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, className, title, style, x, y, w, h, owner, NULL,
                                  inst, param);
    if (!dialog)
        return NULL;
    // The app icon in the title bar: a modal frame shows none unless it's set.
    // LR_SHARED: loaded once, never destroyed.
    HICON bigIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                                      GetSystemMetrics(SM_CYICON), LR_SHARED);
    HICON smallIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                        GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    SendMessageW(dialog, WM_SETICON, ICON_BIG, (LPARAM)bigIcon);
    SendMessageW(dialog, WM_SETICON, ICON_SMALL, (LPARAM)smallIcon);
    ApplyThemedFrame(dialog);
    return dialog;
}

void RunModal(HWND owner, HWND dialog, HWND initialFocus, const bool* done)
{
    // Clicks and keys made while the dialog was being prepared would open it
    // again or land on the disabled owner (a beep): drop them.
    MSG pending;
    while (PeekMessageW(&pending, NULL, WM_MOUSEFIRST, WM_MOUSELAST, PM_REMOVE) ||
           PeekMessageW(&pending, NULL, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE)) {
    }
    EnableWindow(owner, FALSE);
    ShowWindow(dialog, SW_SHOW);
    if (initialFocus)
        SetFocus(initialFocus);

    MSG msg;
    while (!*done) {
        BOOL r = GetMessageW(&msg, NULL, 0, 0);
        if (r <= 0) {
            if (r == 0)
                PostQuitMessage((int)msg.wParam);  // let the main loop see it too
            break;
        }
        if (!IsDialogMessageW(dialog, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    // Re-enable the owner before destroying, so it gets activation back.
    EnableWindow(owner, TRUE);
    DestroyWindow(dialog);
}
