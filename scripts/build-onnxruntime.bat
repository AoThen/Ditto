@echo off
setlocal EnableDelayedExpansion

set VS_VER=%~1
set CRT=%~2
if "%VS_VER%" == "" set VS_VER=v143
if "%CRT%" == "" set CRT=mt

set BUILD_DIR=build-x64-%VS_VER%-%CRT%
set CONFIG=Release
set INSTALL_DIR=%BUILD_DIR%\install
set STATIC_INSTALL_DIR=%BUILD_DIR%\install-static

echo [BUILD] ===================================================================
echo [BUILD] Building ONNX Runtime (static libs)
echo [BUILD]   VS_VER=%VS_VER%  CRT=%CRT%
echo [BUILD]   Build dir: %BUILD_DIR%
echo [BUILD]   Current dir: %CD%
echo [BUILD] ===================================================================

REM -------------------------------------------------------------------
REM 1. Configure via ORT's build.py
REM -------------------------------------------------------------------
echo [BUILD] Step 1/6: Running build.py (configure + build)...

python tools\ci_build\build.py ^
    --build_dir %BUILD_DIR% ^
    --config %CONFIG% ^
    --parallel ^
    --skip_tests ^
    --build_shared_lib ^
    --cmake_generator "Visual Studio 17 2022" ^
    --enable_msvc_static_runtime ^
    --cmake_extra_defines "CMAKE_INSTALL_PREFIX=install" "onnxruntime_BUILD_UNIT_TESTS=OFF" "onnxruntime_ENABLE_PYTHON=OFF"

if errorlevel 1 (
    set ERR=!errorlevel!
    echo [BUILD] ERROR: ORT build.py failed with exit code !ERR!
    endlocal & exit /b !ERR!
)
echo [BUILD] Step 1/6 complete.

REM -------------------------------------------------------------------
REM 2. CMake --install (build.py already built, now install)
REM -------------------------------------------------------------------
echo [BUILD] Step 2/6: cmake --install...

cmake --install "%BUILD_DIR%" --config %CONFIG%
if errorlevel 1 (
    set ERR=!errorlevel!
    echo [BUILD] ERROR: cmake --install failed with exit code !ERR!
    endlocal & exit /b !ERR!
)
echo [BUILD] Step 2/6 complete.

REM Check install directory
if exist "%INSTALL_DIR%" (
    echo [BUILD] Install dir exists: %INSTALL_DIR%
    dir "%INSTALL_DIR%" /s /b >nul 2>&1 && echo [BUILD] Install dir has files || echo [BUILD] WARNING: Install dir is empty
) else (
    echo [BUILD] WARNING: Install dir NOT found at %INSTALL_DIR%
)

REM -------------------------------------------------------------------
REM 3. Collect static libs from the link tlog
REM -------------------------------------------------------------------
echo [BUILD] Step 3/6: Collecting static libs from link tlog...

set TLOG=%BUILD_DIR%\%CONFIG%\onnxruntime.dir\%CONFIG%\onnxruntime.tlog\link.read.1.tlog
echo [BUILD] Looking for tlog: %TLOG%

if not exist "%TLOG%" (
    echo [BUILD] WARNING: link tlog not found at %TLOG%
    echo [BUILD] Skipping lib merge step - will package install dir directly
    goto :package_install
)

echo [BUILD] Tlog found, parsing...

set libs=
for /f "delims=" %%L in ('type "%TLOG%"') do (
    set LINE=%%L
    set LINE=!LINE:^=!
    if "!LINE:~-4!" == ".LIB" (
        set LINE=!LINE:"=!
        set libs=!libs! !LINE!
    )
)

if "!libs!" == "" (
    echo [BUILD] WARNING: No .lib files found in link tlog
    echo [BUILD] Skipping lib merge step - will package install dir directly
    goto :package_install
)

echo [BUILD] Found libs: !libs!
echo [BUILD] Step 3/6 complete.

REM -------------------------------------------------------------------
REM 4. Merge static libs with lib.exe
REM -------------------------------------------------------------------
echo [BUILD] Step 4/6: Merging static libs...

if not exist "%STATIC_INSTALL_DIR%\lib" mkdir "%STATIC_INSTALL_DIR%\lib"

where lib.exe >nul 2>nul
if errorlevel 1 (
    echo [BUILD] WARNING: lib.exe not found, copying libs individually
    for %%L in (!libs!) do (
        copy "%%L" "%STATIC_INSTALL_DIR%\lib\" >nul
        if errorlevel 1 (
            echo [BUILD] WARNING: Failed to copy %%L
        )
    )
) else (
    echo [BUILD] Merging with lib.exe...
    lib.exe /OUT:"%STATIC_INSTALL_DIR%\lib\onnxruntime.lib" !libs!
    if errorlevel 1 (
        echo [BUILD] WARNING: lib.exe failed to merge, copying libs individually
        for %%L in (!libs!) do (
            copy "%%L" "%STATIC_INSTALL_DIR%\lib\" >nul
        )
    ) else (
        echo [BUILD] onnxruntime.lib created successfully
    )
)

echo [BUILD] Step 4/6 complete.

REM -------------------------------------------------------------------
REM 5. Flatten headers
REM -------------------------------------------------------------------
echo [BUILD] Step 5/6: Flattening headers...

if not exist "%STATIC_INSTALL_DIR%\include" mkdir "%STATIC_INSTALL_DIR%\include"

if exist "%INSTALL_DIR%\include\onnxruntime" (
    echo [BUILD] Copying onnxruntime headers...
    xcopy "%INSTALL_DIR%\include\onnxruntime" "%STATIC_INSTALL_DIR%\include" /s /e /y >nul
)

if exist "%INSTALL_DIR%\include\onnxruntime_cxx_api.h" (
    copy "%INSTALL_DIR%\include\onnxruntime_cxx_api.h" "%STATIC_INSTALL_DIR%\include\" >nul
)
if exist "%INSTALL_DIR%\include\onnxruntime_c_api.h" (
    copy "%INSTALL_DIR%\include\onnxruntime_c_api.h" "%STATIC_INSTALL_DIR%\include\" >nul
)

echo [BUILD] Step 5/6 complete.

REM -------------------------------------------------------------------
REM 6. Create OnnxRuntimeConfig.cmake
REM -------------------------------------------------------------------
echo [BUILD] Step 6/6: Creating cmake config...

if not exist "%STATIC_INSTALL_DIR%\lib\cmake\OnnxRuntime" mkdir "%STATIC_INSTALL_DIR%\lib\cmake\OnnxRuntime"
(
    echo set(ONNXRUNTIME_INCLUDE_DIRS "%STATIC_INSTALL_DIR%\include")
    echo set(ONNXRUNTIME_LIBRARIES "%STATIC_INSTALL_DIR%\lib\onnxruntime.lib")
    echo find_package_handle_standard_args(OnnxRuntime DEFAULT_MSG ONNXRUNTIME_LIBRARIES ONNXRUNTIME_INCLUDE_DIRS)
    echo if(NOT ONNXRUNTIME_FOUND)
    echo   return()
    echo endif()
    echo add_library(onnxruntime STATIC IMPORTED)
    echo set_target_properties(onnxruntime PROPERTIES
    echo   IMPORTED_LOCATION "${ONNXRUNTIME_LIBRARIES}"
    echo   INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_INCLUDE_DIRS}"
    echo   INTERFACE_COMPILE_DEFINITIONS "ONNXRUNTIME_STATIC_DEFINE"
    echo )
) > "%STATIC_INSTALL_DIR%\lib\cmake\OnnxRuntime\OnnxRuntimeConfig.cmake"

echo [BUILD] Step 6/6 complete.

:package_install
REM -------------------------------------------------------------------
REM Check if install-static has content, fallback to install dir
REM -------------------------------------------------------------------
if exist "%STATIC_INSTALL_DIR%\lib\onnxruntime.lib" (
    echo [BUILD] ===================================================================
    echo [BUILD] ONNX Runtime static libs built successfully
    echo [BUILD]   Output: %STATIC_INSTALL_DIR%
    echo [BUILD] ===================================================================
    endlocal
    exit /b 0
)

REM Check for any .lib files in install-static
dir "%STATIC_INSTALL_DIR%\lib\*.lib" >nul 2>nul
if not errorlevel 1 (
    echo [BUILD] ===================================================================
    echo [BUILD] ONNX Runtime static libs built (individual libs, no merge)
    echo [BUILD]   Output: %STATIC_INSTALL_DIR%
    echo [BUILD] ===================================================================
    endlocal
    exit /b 0
)

REM Fallback: use install dir content directly
if exist "%INSTALL_DIR%\lib" (
    echo [BUILD] WARNING: install-static empty, using install dir as fallback
    rmdir /s /q "%STATIC_INSTALL_DIR%" 2>nul
    mkdir "%STATIC_INSTALL_DIR%\lib"
    xcopy "%INSTALL_DIR%\lib" "%STATIC_INSTALL_DIR%\lib" /s /e /y >nul
    if exist "%INSTALL_DIR%\include" (
        xcopy "%INSTALL_DIR%\include" "%STATIC_INSTALL_DIR%\include" /s /e /y >nul
    )
    echo [BUILD] ===================================================================
    echo [BUILD] ONNX Runtime packaged from install dir (fallback)
    echo [BUILD]   Output: %STATIC_INSTALL_DIR%
    echo [BUILD] ===================================================================
    endlocal
    exit /b 0
)

echo [BUILD] ERROR: No output artifacts found!
echo [BUILD]   install-static: %STATIC_INSTALL_DIR%
echo [BUILD]   install: %INSTALL_DIR%
echo [BUILD]   build dir: %BUILD_DIR%
if exist "%BUILD_DIR%" (
    echo [BUILD] Contents of build dir (first 30 lines):
    dir "%BUILD_DIR%" /s /b 2>nul
)
endlocal
exit /b 1
