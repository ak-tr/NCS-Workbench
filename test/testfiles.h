// File helpers for the tests that work on folders in %TEMP%.

#pragma once

#include <windows.h>

#include <cassert>
#include <cstring>

#include "../src/util.h"

// Writes (replaces) the file; size defaults to strlen(text).
inline void WriteText(const WString& path, const char* text, size_t size = (size_t)-1)
{
    if (size == (size_t)-1)
        size = strlen(text);
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    assert(h != INVALID_HANDLE_VALUE);
    DWORD written;
    WriteFile(h, text, (DWORD)size, &written, NULL);
    CloseHandle(h);
}

inline String ReadText(const WString& path)
{
    String s;
    bool ok = ReadFileBytes(path, &s);
    assert(ok);
    return s;
}

// Deletes the folder and everything in it, read-only files too (session snapshots are).
inline void RemoveTree(const WString& dir)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(JoinPath(dir, L"*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.cFileName[0] == L'.')
                continue;
            WString path = JoinPath(dir, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                RemoveTree(path);
            } else {
                SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(path.c_str());
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir.c_str());
}
