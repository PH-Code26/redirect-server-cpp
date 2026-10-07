@echo off
setlocal enabledelayedexpansion
title Build URL-Alias-Redirect (C++)
cd /d "%~dp0"

echo ==============================================
echo   Building URL Alias Redirect (C++)
echo ==============================================
echo.

rem ---------- step 1: find cl.exe ----------
echo [1/3] Finding MSVC compiler...
set CL_EXE=
for /f "tokens=*" %%i in ('where cl.exe 2^>nul') do set CL_EXE=%%i
if defined CL_EXE goto :has_cl
for /d %%d in ("C:\Program Files\Microsoft Visual Studio\2022\*\VC\Auxiliary\Build\vcvars64.bat") do (
    call "%%d" >nul 2>&1
    for /f "tokens=*" %%i in ('where cl.exe 2^>nul') do set CL_EXE=%%i
    if defined CL_EXE goto :has_cl
)
for /d %%d in ("C:\Program Files (x86)\Microsoft Visual Studio\2019\*\VC\Auxiliary\Build\vcvars64.bat") do (
    call "%%d" >nul 2>&1
    for /f "tokens=*" %%i in ('where cl.exe 2^>nul') do set CL_EXE=%%i
    if defined CL_EXE goto :has_cl
)
echo   [FAIL] cl.exe not found.
echo   Install: winget install Microsoft.VisualStudio.2022.BuildTools
pause
exit /b 1
:has_cl
echo   [OK] %CL_EXE%

rem ---------- step 2: find or install OpenSSL ----------
echo [2/3] Finding OpenSSL...
set SSL_ROOT=
set SSL_INC=
set SSL_LIB=

for %%p in (
    "C:\Program Files\OpenSSL-Win64"
    "C:\Program Files\OpenSSL"
) do (
    if exist "%%~p\include\openssl\ssl.h" (
        set SSL_ROOT=%%~p
        set SSL_INC=%%~p\include
        set SSL_LIB=%%~p\lib
        goto :ssl_found
    )
)

echo   Not found. Installing via winget...
winget install ShiningLight.OpenSSL.Dev --accept-source-agreements 2>&1

for %%p in (
    "C:\Program Files\OpenSSL-Win64"
    "C:\Program Files\OpenSSL"
) do (
    if exist "%%~p\include\openssl\ssl.h" (
        set SSL_ROOT=%%~p
        set SSL_INC=%%~p\include
        set SSL_LIB=%%~p\lib
        goto :ssl_found
    )
)

echo   [FAIL] OpenSSL install failed.
echo   Download "Win64 OpenSSL v3.x" (NOT Light) from:
echo     https://slproweb.com/products/Win32OpenSSL.html
echo   Install to: C:\Program Files\OpenSSL-Win64
pause
exit /b 1

:ssl_found
set SSL_LIB=%SSL_ROOT%\lib\VC\x64\MT
if not exist "%SSL_LIB%\libssl_static.lib" set SSL_LIB=%SSL_ROOT%\lib\VC\x64\MD
if not exist "%SSL_LIB%\libssl.lib" (
    echo   [FAIL] Cannot find libssl.lib / libssl_static.lib
    pause
    exit /b 1
)
echo   [OK] %SSL_ROOT%  (libs: %SSL_LIB%)

rem ---------- step 3: compile ----------
echo [3/3] Compiling with cl.exe...

if exist "%SSL_LIB%\libssl_static.lib" (
    set SSL_LIBS=libssl_static.lib libcrypto_static.lib
    echo   Using static OpenSSL (single exe, no DLLs needed)
) else (
    set SSL_LIBS=libssl.lib libcrypto.lib
)

cl.exe /nologo /EHsc /O2 /MT /std:c++17 ^
    /I"%SSL_INC%" ^
    server.cpp ^
    /link /LIBPATH:"%SSL_LIB%" ^
    !SSL_LIBS! crypt32.lib ws2_32.lib ^
    /OUT:url-alias-redirect.exe

if %errorlevel% neq 0 (
    echo   [FAIL] Compilation failed.
    pause
    exit /b 1
)

if not "!SSL_LIBS!"=="libssl_static.lib libcrypto_static.lib" (
    set SSL_BIN=%SSL_ROOT%\bin
    if exist "!SSL_BIN!\libssl-3-x64.dll" (
        copy /y "!SSL_BIN!\libssl-3-x64.dll" "%~dp0" >nul 2>&1
        copy /y "!SSL_BIN!\libcrypto-3-x64.dll" "%~dp0" >nul 2>&1
        echo   Copied libssl-3-x64.dll + libcrypto-3-x64.dll
    )
)

echo.
echo ==============================================
echo   Build successful!
echo   Binary: url-alias-redirect.exe
echo ==============================================
echo.
echo Now run: setup.bat  (as Administrator)
echo.
pause