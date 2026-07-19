@echo off
setlocal EnableExtensions DisableDelayedExpansion

set "DXMT_BUILDER_SCRIPT=%~dp0"
for %%I in ("%DXMT_BUILDER_SCRIPT%..") do set "DXMT_REPO_ROOT=%%~fI"
set "DXMT_BUILDER_ROOT=%DXMT_REPO_ROOT%\.cache\managed\bootstrap\dxmt-builder-windows"
set "DXMT_BUILDER_EXE=%DXMT_BUILDER_ROOT%\dxmt-builder.exe"

call :load_vs_clang
if errorlevel 1 exit /b %ERRORLEVEL%

if not exist "%DXMT_BUILDER_ROOT%" mkdir "%DXMT_BUILDER_ROOT%"
if errorlevel 1 exit /b %ERRORLEVEL%

pushd "%DXMT_BUILDER_ROOT%"
if errorlevel 1 exit /b %ERRORLEVEL%
clang-cl.exe /nologo /std:c++20 /EHsc /O2 /DNOMINMAX /DWIN32_LEAN_AND_MEAN ^
  /D_WIN32_WINNT=0x0A00 ^
  /I"%DXMT_REPO_ROOT%\tools\dxmt-builder\src" ^
  "%DXMT_REPO_ROOT%\tools\dxmt-builder\src\main.cpp" ^
  "%DXMT_REPO_ROOT%\tools\dxmt-builder\src\builder.cpp" ^
  "%DXMT_REPO_ROOT%\tools\dxmt-builder\src\sha256.cpp" ^
  /Fe:"%DXMT_BUILDER_EXE%" -fuse-ld=lld /link /INCREMENTAL:NO
set "DXMT_BUILD_EXIT=%ERRORLEVEL%"
popd
if not "%DXMT_BUILD_EXIT%"=="0" exit /b %DXMT_BUILD_EXIT%

"%DXMT_BUILDER_EXE%" %*
exit /b %ERRORLEVEL%

:load_vs_clang
if exist "%ProgramFiles%\LLVM\bin" set "PATH=%ProgramFiles%\LLVM\bin;%PATH%"
if exist "%ProgramFiles%\Meson" set "PATH=%ProgramFiles%\Meson;%PATH%"
set "DXMT_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%DXMT_VSWHERE%" (
  echo dxmt-builder: Visual Studio environment discovery was not found. 1>&2
  exit /b 2
)
set "DXMT_VS_PATH="
for /f "usebackq tokens=*" %%I in (`"%DXMT_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Llvm.Clang -property installationPath`) do set "DXMT_VS_PATH=%%I"
if not defined DXMT_VS_PATH (
  echo dxmt-builder: Visual Studio with the Clang component was not found. 1>&2
  exit /b 2
)
call "%DXMT_VS_PATH%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %ERRORLEVEL%
where clang-cl.exe >nul 2>&1
if errorlevel 1 (
  echo dxmt-builder: clang-cl.exe is unavailable after VsDevCmd. 1>&2
  exit /b 2
)
where lld-link.exe >nul 2>&1
if errorlevel 1 (
  echo dxmt-builder: lld-link.exe is unavailable after VsDevCmd. 1>&2
  exit /b 2
)
where llvm-lib.exe >nul 2>&1
if errorlevel 1 (
  echo dxmt-builder: llvm-lib.exe is unavailable after VsDevCmd. 1>&2
  exit /b 2
)
where meson.exe >nul 2>&1
if errorlevel 1 (
  echo dxmt-builder: meson.exe is unavailable after VsDevCmd. 1>&2
  exit /b 2
)
where ninja.exe >nul 2>&1
if errorlevel 1 (
  echo dxmt-builder: ninja.exe is unavailable after VsDevCmd. 1>&2
  exit /b 2
)
exit /b 0
