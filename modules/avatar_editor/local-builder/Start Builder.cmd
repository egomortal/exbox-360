@echo off
setlocal
rem SPDX-License-Identifier: GPL-3.0-or-later
powershell.exe -NoLogo -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0Launch Editor Builder.ps1"
set "_EDITOR_LAUNCH_EXIT=%ERRORLEVEL%"
if not "%_EDITOR_LAUNCH_EXIT%"=="0" (
    echo The launcher could not start. Review the error above.
    echo If Windows blocked a downloaded file, use Properties - Unblock on the trusted download before extracting it.
    pause
)
exit /b %_EDITOR_LAUNCH_EXIT%
