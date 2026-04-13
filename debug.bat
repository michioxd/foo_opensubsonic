@echo off
title Quick debug

echo Building...
msbuild .\foo_opensubsonic.sln /t:Build /p:Configuration=Debug /p:Platform=x64

if %errorlevel% neq 0 (
    echo.
    echo Build failed. Quiting...
    exit /b %errorlevel%
)

echo.
echo Copying file...

if not exist "%appdata%\foobar2000-v2\user-components-x64\foo_opensubsonic\" (
    mkdir "%appdata%\foobar2000-v2\user-components-x64\foo_opensubsonic\"
)

copy /Y ".\x64\Debug\foo_opensubsonic.dll" "%appdata%\foobar2000-v2\user-components-x64\foo_opensubsonic\foo_opensubsonic.dll"

echo.
echo OK!