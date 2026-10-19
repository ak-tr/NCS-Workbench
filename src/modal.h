// Dark modal popups (Backup TRC, Empty MAN).

#pragma once

#include <windows.h>

// Creates a hidden popup with the given client size, centred on its owner,
// with a dark frame. `param` arrives as CREATESTRUCT::lpCreateParams.
HWND CreateModal(HWND owner, const wchar_t* className, WNDPROC proc, const wchar_t* title, int clientWidth,
                 int clientHeight, void* param, bool resizable = false);

// Window size for a client size, matching CreateModal's styles (WM_GETMINMAXINFO).
SIZE ModalWindowSize(int clientWidth, int clientHeight, bool resizable);

// Disables the owner, shows the dialog and pumps messages until *done, then
// destroys the dialog. A WM_QUIT is passed on to the main loop.
void RunModal(HWND owner, HWND dialog, HWND initialFocus, const bool* done);
