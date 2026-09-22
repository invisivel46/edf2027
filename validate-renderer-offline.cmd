@echo off
setlocal
rem Preserve the configured SDK. Only initialize the compiler environment.
if defined VSCMD_VER goto ready
if not defined VSDEVCMD set "VSDEVCMD=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"
if not exist "%VSDEVCMD%" (
  echo ERROR: Set VSDEVCMD to your Visual Studio Common7\Tools\VsDevCmd.bat.
  exit /b 1
)
call "%VSDEVCMD%" -no_logo -arch=amd64 -host_arch=amd64
if errorlevel 1 exit /b %errorlevel%
:ready
python "%~dp0tools\validate-renderer-offline.py" %*
exit /b %errorlevel%
