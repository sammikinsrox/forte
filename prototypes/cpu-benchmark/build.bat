@echo off
rem PROTOTYPE build script for the Forte CPU benchmark (ticket 0011).
rem Builds build\bench_avx2.exe (/arch:AVX2, FMA) and build\bench_sse2.exe
rem (baseline x64 = SSE2) with MSVC, then runs them for 30 s of simulated audio.
rem Usage: build.bat            (build + run)
rem        build.bat nobench    (build only)
rem A "'vswhere.exe' is not recognized" line printed by vcvars64.bat is harmless.
setlocal
cd /d "%~dp0"
if defined VCINSTALLDIR goto :build
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR echo MSVC not found & exit /b 1
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
:build
if not exist build mkdir build
cl /nologo /std:c++17 /O2 /Oi /fp:fast /arch:AVX2 /EHsc bench.cpp /Fobuild\bench_avx2.obj /Febuild\bench_avx2.exe || exit /b 1
cl /nologo /std:c++17 /O2 /Oi /fp:fast /EHsc bench.cpp /Fobuild\bench_sse2.obj /Febuild\bench_sse2.exe || exit /b 1
if /i "%~1"=="nobench" exit /b 0
build\bench_avx2.exe --seconds 30
build\bench_sse2.exe --seconds 30
build\bench_avx2.exe --seconds 30 --chords
build\bench_sse2.exe --seconds 30 --chords
