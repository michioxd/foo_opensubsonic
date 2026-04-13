@echo off
setlocal enabledelayedexpansion
title Build foo_opensubsonic

set "ROOT_DIR=%~dp0"
cd /d "%ROOT_DIR%"

set "PROJECT=src\foo_opensubsonic.vcxproj"
set "DIST_DIR=%ROOT_DIR%dist"
set "STAGE_DIR=%DIST_DIR%\foo_opensubsonic"
set "PACKAGE_NAME=foo_opensubsonic.fb2k-component"
set "PACKAGE_PATH=%DIST_DIR%\%PACKAGE_NAME%"
set "X86_DLL=%ROOT_DIR%\src\Release\foo_opensubsonic.dll"
set "X64_DLL=%ROOT_DIR%\src\x64\Release\foo_opensubsonic.dll"
set "ARM64EC_DLL=%ROOT_DIR%\src\ARM64EC\Release\foo_opensubsonic.dll"

where msbuild >nul 2>nul
if errorlevel 1 (
	echo [ERROR] msbuild was not found in PATH.
	exit /b 1
)

where 7z >nul 2>nul
if errorlevel 1 (
	echo [ERROR] 7z was not found in PATH.
	exit /b 1
)

if not exist "%DIST_DIR%" mkdir "%DIST_DIR%"
if exist "%STAGE_DIR%" rmdir /s /q "%STAGE_DIR%"
mkdir "%STAGE_DIR%"
mkdir "%STAGE_DIR%\x64"
mkdir "%STAGE_DIR%\arm64ec"

echo Building foo_opensubsonic x86 Release...
msbuild "%PROJECT%" /maxcpucount /p:RestorePackagesConfig=true /p:Configuration=Release /p:Platform=Win32 /p:UseOfAtl=Static /t:restore,build
if errorlevel 1 (
	echo [ERROR] x86 build failed.
	exit /b 1
)

echo Building foo_opensubsonic x64 Release...
msbuild "%PROJECT%" /maxcpucount /p:RestorePackagesConfig=true /p:Configuration=Release /p:Platform=x64 /p:UseOfAtl=Static /t:restore,build
if errorlevel 1 (
	echo [ERROR] x64 build failed.
	exit /b 1
)

echo Building foo_opensubsonic ARM64EC Release...
msbuild "%PROJECT%" /maxcpucount /p:RestorePackagesConfig=true /p:Configuration=Release /p:Platform=ARM64EC /p:UseOfAtl=Static /t:restore,build
if errorlevel 1 (
	echo [ERROR] ARM64EC build failed.
	exit /b 1
)

if not exist "%X86_DLL%" (
	echo [ERROR] Missing output: "%X86_DLL%"
	exit /b 1
)

if not exist "%X64_DLL%" (
	echo [ERROR] Missing output: "%X64_DLL%"
	exit /b 1
)

if not exist "%ARM64EC_DLL%" (
	echo [ERROR] Missing output: "%ARM64EC_DLL%"
	exit /b 1
)

echo Packaging foo_opensubsonic...
copy /y "%X86_DLL%" "%STAGE_DIR%\foo_opensubsonic.dll" >nul
if errorlevel 1 (
	echo [ERROR] Failed to copy x86 DLL.
	exit /b 1
)

copy /y "%X64_DLL%" "%STAGE_DIR%\x64\foo_opensubsonic.dll" >nul
if errorlevel 1 (
	echo [ERROR] Failed to copy x64 DLL.
	exit /b 1
)

copy /y "%ARM64EC_DLL%" "%STAGE_DIR%\arm64ec\foo_opensubsonic.dll" >nul
if errorlevel 1 (
	echo [ERROR] Failed to copy ARM64EC DLL.
	exit /b 1
)

if exist "%PACKAGE_PATH%" del /f /q "%PACKAGE_PATH%"

pushd "%STAGE_DIR%"
if errorlevel 1 (
	echo [ERROR] Failed to enter staging directory.
	exit /b 1
)

7z a -tzip -mx9 "%PACKAGE_PATH%" "foo_opensubsonic.dll" "x64\foo_opensubsonic.dll" "arm64ec\foo_opensubsonic.dll"
set "ZIP_EXIT=%ERRORLEVEL%"
popd

if not "%ZIP_EXIT%"=="0" (
	echo [ERROR] Failed to create fb2k-component package.
	exit /b %ZIP_EXIT%
)

echo Done: "%PACKAGE_PATH%"
exit /b 0
