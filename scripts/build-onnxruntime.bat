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

echo ===================================================================
echo Building ONNX Runtime (static libs)
echo   VS_VER=%VS_VER%  CRT=%CRT%
echo   Build dir: %BUILD_DIR%
echo ===================================================================

REM -------------------------------------------------------------------
REM 1. Configure via ORT's build.py
REM    CMAKE_INSTALL_PREFIX is relative to the build directory created by build.py
REM -------------------------------------------------------------------
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
    echo ERROR: ORT build.py configuration failed (exit code !errorlevel!)
    exit /b !errorlevel!
)

REM -------------------------------------------------------------------
REM 2. Build and install via CMake
REM -------------------------------------------------------------------
echo Building with CMake (parallel)...
cmake --build "%BUILD_DIR%" --config %CONFIG% -j %NUMBER_OF_PROCESSORS%
if errorlevel 1 (
    echo ERROR: CMake build failed
    endlocal
    exit /b !errorlevel!
)

echo Installing...
cmake --install "%BUILD_DIR%" --config %CONFIG%
if errorlevel 1 (
    echo ERROR: CMake install failed
    endlocal
    exit /b !errorlevel!
)

REM -------------------------------------------------------------------
REM 3. Collect static libs from the link tlog
REM -------------------------------------------------------------------
echo Collecting static libs from link tlog...
set TLOG=%BUILD_DIR%\%CONFIG%\onnxruntime.dir\%CONFIG%\onnxruntime.tlog\link.read.1.tlog

if not exist "%TLOG%" (
    echo ERROR: link tlog not found at %TLOG%
    endlocal
    exit /b 1
)

for /f "delims=" %%L in ('type "%TLOG%"') do (
    set LINE=%%L
    set LINE=!LINE:^=!
    if "!LINE:~-4!" == ".LIB" (
        set LINE=!LINE:"=!
        set libs=!libs! !LINE!
    )
)

if "!libs!" == "" (
    echo ERROR: No .lib files found in link tlog
    endlocal
    exit /b 1
)

echo Found static libs to merge.

REM -------------------------------------------------------------------
REM 4. Merge static libs with lib.exe
REM -------------------------------------------------------------------
echo Creating install-static directories...
if not exist "%STATIC_INSTALL_DIR%\lib" mkdir "%STATIC_INSTALL_DIR%\lib"

where lib.exe >nul 2>nul
if errorlevel 1 (
    echo WARNING: lib.exe not found, copying libs individually
    for %%L in (!libs!) do (
        copy "%%L" "%STATIC_INSTALL_DIR%\lib\" >nul
        if errorlevel 1 (
            echo ERROR: Failed to copy %%L
            endlocal
            exit /b 1
        )
    )
) else (
    echo Merging static libs into single onnxruntime.lib...
    lib.exe /OUT:"%STATIC_INSTALL_DIR%\lib\onnxruntime.lib" !libs!
    if errorlevel 1 (
        echo ERROR: lib.exe failed to merge static libs
        endlocal
        exit /b 1
    )
    echo onnxruntime.lib created successfully
)

REM -------------------------------------------------------------------
REM 5. Flatten headers (xcopy recursively, flattening subdirs)
REM -------------------------------------------------------------------
echo Flattening headers...
if not exist "%STATIC_INSTALL_DIR%\include" mkdir "%STATIC_INSTALL_DIR%\include"

if exist "%INSTALL_DIR%\include\onnxruntime" (
    xcopy "%INSTALL_DIR%\include\onnxruntime" "%STATIC_INSTALL_DIR%\include" /s /e /y
    if errorlevel 1 (
        echo ERROR: Failed to flatten onnxruntime headers
        endlocal
        exit /b 1
    )
)

if exist "%INSTALL_DIR%\include\onnxruntime_cxx_api.h" (
    copy "%INSTALL_DIR%\include\onnxruntime_cxx_api.h" "%STATIC_INSTALL_DIR%\include\" >nul
)
if exist "%INSTALL_DIR%\include\onnxruntime_c_api.h" (
    copy "%INSTALL_DIR%\include\onnxruntime_c_api.h" "%STATIC_INSTALL_DIR%\include\" >nul
)

REM -------------------------------------------------------------------
REM 6. Create OnnxRuntimeConfig.cmake
REM -------------------------------------------------------------------
echo Creating OnnxRuntimeConfig.cmake...
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

REM -------------------------------------------------------------------
REM Done
REM -------------------------------------------------------------------
echo ===================================================================
echo ONNX Runtime static libs built successfully
echo   Output: %STATIC_INSTALL_DIR%
echo ===================================================================
endlocal
