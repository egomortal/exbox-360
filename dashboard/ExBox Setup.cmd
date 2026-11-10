@echo off
rem Ex-Box Setup: one folder, one button. Opens the installer window in your browser.
rem (The older drop-files window is still available: py -3 launch_setup.py)
setlocal
cd /d "%~dp0"
where py >nul 2>nul && (py -3 -B -m installer %* & goto done)
where python >nul 2>nul && (python -B -m installer %* & goto done)
where wsl >nul 2>nul && (wsl.exe -e python3 -B -m installer %* & goto done)
echo Ex-Box Setup needs Python 3.11 or newer. Install it from python.org, then run this again.
pause
:done
endlocal
