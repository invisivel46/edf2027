@echo off
setlocal

set "PROJECT_DIR=%~dp0"
set "VS_ROOT=C:\Program Files\Microsoft Visual Studio\18\Community"
set "VSDEVCMD=%VS_ROOT%\Common7\Tools\VsDevCmd.bat"

if not defined REXGLUE_SDK set "REXGLUE_SDK=D:\roms2\edf3-translation-project\rexglue\sdk\win-amd64"
if not defined BUILD_JOBS set "BUILD_JOBS=2"
if "%~1"=="" (set "BUILD_PRESET=win-amd64-release") else (set "BUILD_PRESET=%~1")
if "%~2"=="" (set "BUILD_TARGET=edf2017") else (set "BUILD_TARGET=%~2")

if not exist "%VSDEVCMD%" (
  echo ERROR: Visual Studio developer environment not found:
  echo   %VSDEVCMD%
  exit /b 1
)

if not exist "%REXGLUE_SDK%\lib\cmake\rexglue" (
  echo ERROR: ReXGlue SDK not found:
  echo   %REXGLUE_SDK%
  echo Set REXGLUE_SDK to the SDK's win-amd64 directory and try again.
  exit /b 1
)

call "%VSDEVCMD%" -arch=amd64 -host_arch=amd64
if errorlevel 1 exit /b %errorlevel%

pushd "%PROJECT_DIR%"
echo Configuring %BUILD_PRESET%...
cmake --preset "%BUILD_PRESET%" -DCMAKE_PREFIX_PATH="%REXGLUE_SDK%"
if errorlevel 1 (
  set "RESULT=%errorlevel%"
  popd
  exit /b %RESULT%
)

echo Building %BUILD_TARGET% with %BUILD_JOBS% parallel job(s)...
cmake --build --preset "%BUILD_PRESET%" --target "%BUILD_TARGET%" --parallel "%BUILD_JOBS%"
set "RESULT=%errorlevel%"
popd

if not "%RESULT%"=="0" exit /b %RESULT%
echo Build completed successfully.
exit /b 0
