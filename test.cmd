@echo off
rem Builds and runs the tests in test\ (same toolchain and runtime rules as build.cmd).
setlocal
cd /d "%~dp0"

set CXX=i686-w64-mingw32-g++
where %CXX% >nul 2>nul || if exist "%LOCALAPPDATA%\llvm-mingw\bin" set "PATH=%LOCALAPPDATA%\llvm-mingw\bin;%PATH%"
where %CXX% >nul 2>nul || (echo %CXX% not found. & exit /b 1)

if not exist build mkdir build
call :run trace_test || exit /b 1
call :run ncsdata_test || exit /b 1
call :run translations_test || exit /b 1
call :run paramindex_test || exit /b 1
call :run util_test src\util.cpp -lshell32 -lole32 -ladvapi32 || exit /b 1
call :run session_test src\session.cpp src\util.cpp -lshell32 -lole32 -ladvapi32 || exit /b 1
call :run translator_test src\translator.cpp src\util.cpp -lshell32 -lole32 -ladvapi32 || exit /b 1
call :run daten_test src\daten.cpp src\util.cpp -lshell32 -lole32 -ladvapi32 || exit /b 1
exit /b 0

:run
rem %1 = test name, %2.. = extra sources and libraries it needs
%CXX% -std=c++17 -Wall -Wextra -fno-exceptions -fno-rtti -fno-threadsafe-statics -static -O1 ^
  -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0501 -D_WIN32_IE=0x0600 ^
  test\%1.cpp src\xpstd.cpp %2 %3 %4 %5 %6 %7 -o build\%1.exe || (echo Build of %1 failed. & exit /b 1)
build\%1.exe