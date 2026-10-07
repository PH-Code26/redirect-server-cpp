@echo off
title URL-Alias-Redirect (C++)
cd /d "%~dp0"
echo.
echo =========================================
echo       URL Alias Redirect Service (C++)
echo =========================================
echo.
echo Listening on port 5666 (forwarded from 80)
echo Press Ctrl+C to stop
echo.
bin\url-alias-redirect.exe
pause