@echo off
setlocal enabledelayedexpansion

echo ========================================================
echo  exDesktop Windows Native Builder with Compiler Cache
echo ========================================================

set "BUILD_DIR=out"
set "CONFIG=Release"
if not "%~1"=="" set "BUILD_DIR=%~1"
if not "%~2"=="" set "CONFIG=%~2"

:: Check for sccache / ccache
where sccache >nul 2>&1
if %errorlevel% equ 0 (
    echo [INFO] Detected sccache in PATH. Enabling object file caching.
    set "CACHE_FLAG=-DDESKTOP_APP_USE_COMPILER_CACHE=ON"
) else (
    where ccache >nul 2>&1
    if %errorlevel% equ 0 (
        echo [INFO] Detected ccache in PATH. Enabling object file caching.
        set "CACHE_FLAG=-DDESKTOP_APP_USE_COMPILER_CACHE=ON"
    ) else (
        echo [INFO] Neither sccache nor ccache found in PATH.
        echo [TIP]  Install sccache (cargo install sccache or scoop install sccache) for 2x faster rebuilds.
        set "CACHE_FLAG=-DDESKTOP_APP_USE_COMPILER_CACHE=OFF"
    )
)

if not "%TDESKTOP_API_ID%"=="" (
    set "API_ARGS=-DTDESKTOP_API_ID=%TDESKTOP_API_ID% -DTDESKTOP_API_HASH=%TDESKTOP_API_HASH%"
) else (
    set "API_ARGS=-DTDESKTOP_API_TEST=ON"
)

echo [INFO] Configuring CMake in %BUILD_DIR% (%CONFIG%)...
cmake -B "%BUILD_DIR%" ^
    -DCMAKE_BUILD_TYPE="%CONFIG%" ^
    %API_ARGS% ^
    %CACHE_FLAG%

if %errorlevel% neq 0 (
    echo [ERROR] CMake configuration failed.
    exit /b %errorlevel%
)

echo [INFO] To compile, run: cmake --build %BUILD_DIR% --config %CONFIG% --target Telegram
exit /b 0
