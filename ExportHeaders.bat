@echo off
setlocal

set "SOURCE_DIR=%~dp0miniGraph\include\MiniGraph"
set "DEST_DIR=%~dp0ExportHeaders\MiniGraph"

if not exist "%SOURCE_DIR%\" (
    echo [ERROR] miniGraph public header directory was not found:
    echo         "%SOURCE_DIR%"
    exit /b 1
)

if not exist "%DEST_DIR%\" (
    mkdir "%DEST_DIR%"
    if errorlevel 1 (
        echo [ERROR] Could not create the export directory:
        echo         "%DEST_DIR%"
        exit /b 1
    )
)

xcopy "%SOURCE_DIR%\*.h" "%DEST_DIR%\" /Y /I >NUL
if errorlevel 1 (
    echo [ERROR] Failed to copy miniGraph public headers.
    exit /b 1
)

echo miniGraph public headers were copied to:
echo "%DEST_DIR%"
exit /b 0
