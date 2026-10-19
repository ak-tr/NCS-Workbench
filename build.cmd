@echo off
rem Builds build\NCS-Workbench.exe (static, 32-bit, XP+) and checks its imports.
rem Needs an msvcrt-based i686 MinGW-w64 toolchain (e.g. llvm-mingw msvcrt),
rem either on PATH or extracted to %LOCALAPPDATA%\llvm-mingw.
setlocal
cd /d "%~dp0"

set PREFIX=i686-w64-mingw32-
set CXX=%PREFIX%g++
set WINDRES=%PREFIX%windres
where %CXX% >nul 2>nul || if exist "%LOCALAPPDATA%\llvm-mingw\bin" set "PATH=%LOCALAPPDATA%\llvm-mingw\bin;%PATH%"
where %CXX% >nul 2>nul || (echo %CXX% not found. & exit /b 1)
set OUT=build\NCS-Workbench.exe
set OBJDUMP=llvm-objdump
where %OBJDUMP% >nul 2>nul || set OBJDUMP=objdump

if not exist build mkdir build

%WINDRES% -I src -I . src\app.rc -O coff -o build\app.res || goto :fail

rem -fno-exceptions/-fno-rtti/-fno-threadsafe-statics + src\xpstd.cpp keep
rem libc++abi/libunwind (Vista-only imports) out of the exe; see src\xpstd.h.
%CXX% -std=c++17 -Wall -Wextra -fno-exceptions -fno-rtti -fno-threadsafe-statics ^
  -static -municode -mwindows -Os -s ^
  -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 -D_WIN32_IE=0x0600 ^
  -Wl,--major-subsystem-version,5,--minor-subsystem-version,1 ^
  -Wl,--major-os-version,5,--minor-os-version,1 ^
  src\main.cpp src\backup.cpp src\compare.cpp src\daten.cpp src\man.cpp src\modal.cpp src\theme.cpp src\util.cpp src\xpstd.cpp ^
  build\app.res ^
  -o %OUT% ^
  -lcomctl32 -lcomdlg32 -lshell32 -lole32 -ladvapi32 || goto :fail

echo.
echo Imported DLLs:
%OBJDUMP% -p %OUT% | findstr /C:"DLL Name"

rem UCRT imports = wrong toolchain; won't run on XP or clean Win7.
%OBJDUMP% -p %OUT% | findstr /I /C:"api-ms-win-crt" /C:"ucrtbase" >nul && (
  echo FAIL: UCRT imports found. Use an msvcrt toolchain.
  exit /b 1
)

rem Common Vista+ kernel32 imports pulled in by modern C++ runtimes.
%OBJDUMP% -p %OUT% | findstr /R /C:"SRWLock" /C:"ConditionVariable" /C:"InitOnce" /C:"GetTickCount64" /C:"PreciseAsFileTime" /C:"FlsAlloc" /C:"GetFileInformationByHandleEx" /C:"CreateSymbolicLink" && (
  echo FAIL: Vista+ API imports found, exe will not start on XP.
  exit /b 1
)

echo.
echo OK: %OUT%
exit /b 0

:fail
echo Build failed.
exit /b 1
