@echo off
setlocal enabledelayedexpansion

set "ROOT=%~dp0"
cd /d "%ROOT%"

set "PYTHON="
for %%P in (py python python3) do (
	if "!PYTHON!"=="" (
		%%P -c "import sys; sys.exit(0 if sys.version_info.major == 3 else 1)" >nul 2>&1
		if !errorlevel! equ 0 set "PYTHON=%%P"
	)
)

if "!PYTHON!"=="" (
	echo [!] Python 3 was not found in PATH.
	exit /b 1
)

!PYTHON! "%ROOT%py2c_windows.py" %*
exit /b !errorlevel!
