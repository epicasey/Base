@echo off
rem Uploads a project's OTA\firmware.json and the .bin it names (see make_ota_package.py's post-build step in the
rem project's CMakeLists.txt) to the OTA server, via WinSCP's saved "OTA" site (host/credentials stored there, not here).
rem
rem Shared by every project that uses base/: wired to the ESP-IDF status bar's "Execute Custom Task" button by
rem   "idf.customTask": "..\\base\\upload_ota.cmd"
rem in the VS Code user settings. The extension runs that from the open project's folder, so the relative path only
rem resolves in projects that sit next to base/, and the project is the current directory - not this script's folder.
rem The project folder can also be passed as the first argument.
rem (idf.customTask points at a script, with no quoting of its own, because a quoted command embedded directly in
rem that setting came through to the shell with its quotes double-escaped by the ESP-IDF extension.)
rem
rem The remote folder and .bin name come from firmware.json's "file" URL (https://<host>/<remote folder>/<name>.bin),
rem i.e. from the project's Common.h FIRMWARE_UPGRADE_URL and project() name, so the upload goes where the devices
rem will look for it.
setlocal
set "PROJECT=%~1"
if not defined PROJECT set "PROJECT=%CD%"
set "OTA=%PROJECT%\OTA"
if not exist "%OTA%\firmware.json" (
    echo %OTA%\firmware.json not found - build the project first.
    exit /b 1
)

rem {"version":1.00,"file":"https://host/remote_dir/name.bin"} split on / and " -> token 8 = remote_dir, 9 = name.bin
rem (options unquoted with ^-escapes: a " delimiter can't go inside the usual quoted option string)
for /f usebackq^ tokens^=8^,9^ delims^=/^" %%a in ("%OTA%\firmware.json") do (
    set "REMOTE=%%a"
    set "BIN=%%b"
)
if not defined BIN (
    echo Could not read the file URL from %OTA%\firmware.json
    exit /b 1
)
if not exist "%OTA%\%BIN%" (
    echo %OTA%\%BIN% not found
    exit /b 1
)

echo Uploading firmware.json and %BIN% to /%REMOTE%/
"C:\Program Files (x86)\WinSCP\WinSCP.com" /command "open OTA" "put ""%OTA%\firmware.json"" ""%OTA%\%BIN%"" /%REMOTE%/" "exit"
exit /b %ERRORLEVEL%
