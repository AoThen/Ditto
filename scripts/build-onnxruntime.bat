@echo off
setlocal EnableDelayedExpansion

set VS_VER=%~1
set CRT=%~2
if "%VS_VER%" == "" set VS_VER=v143
if "%CRT%" == "" set CRT=mt

set BUILD_DIR=build-x64-%VS_VER%-%CRT%
set CONFIG=Release
set INSTALL_DIR=%BUILD_DIR%\%CONFIG%\install
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
REM 1a. Build absl as standalone static library
REM -------------------------------------------------------------------
REM absl is fetched via FetchContent with EXCLUDE_FROM_ALL, so it is
REM downloaded but NOT compiled by the main build. We build it explicitly
REM so that re2 (which depends on absl) can be built standalone.

echo [BUILD] Step 1a/6: Building absl from source...

set "ABSL_SRC_DIR=%BUILD_DIR%\%CONFIG%\_deps\abseil_cpp-src"
set "ABSL_BUILD_DIR=%BUILD_DIR%\absl-external"
set "ABSL_INSTALL_DIR=%BUILD_DIR%\absl-install"

if exist "%ABSL_SRC_DIR%\CMakeLists.txt" (
    echo [BUILD]   Found absl source at: %ABSL_SRC_DIR%

    cmake -S "%ABSL_SRC_DIR%" -B "%ABSL_BUILD_DIR%" ^
        -G "Visual Studio 17 2022" ^
        -A x64 ^
        -DCMAKE_BUILD_TYPE=%CONFIG% ^
        -DCMAKE_CXX_STANDARD=17 ^
        -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded ^
        -DBUILD_SHARED_LIBS=OFF ^
        -DABSL_BUILD_TESTING=OFF

    if errorlevel 1 (
        echo [BUILD] WARNING: absl cmake configure failed
    ) else (
        cmake --build "%ABSL_BUILD_DIR%" --config %CONFIG%
        if errorlevel 1 (
            echo [BUILD] WARNING: absl build failed
        ) else (
            echo [BUILD]   absl built successfully
            cmake --install "%ABSL_BUILD_DIR%" --config %CONFIG% --prefix "%ABSL_INSTALL_DIR%"
            if errorlevel 1 (
                echo [BUILD] WARNING: absl install failed
            ) else (
                echo [BUILD]   absl installed to %ABSL_INSTALL_DIR%
            )
        )
    )
) else (
    echo [BUILD] WARNING: absl source not found at %ABSL_SRC_DIR%
)

echo [BUILD] Step 1a/6 complete.

REM -------------------------------------------------------------------
REM 1b. Build re2 as standalone static library
REM -------------------------------------------------------------------
REM ONNX Runtime fetches re2 via FetchContent with EXCLUDE_FROM_ALL, so the
REM re2 source is downloaded but NOT compiled by the main build. We build it
REM explicitly to ensure the static library is available for merging.
REM re2 depends on absl, which we build standalone in Step 1a.

echo [BUILD] Step 1b/6: Building re2 as standalone static library...

set "RE2_SRC_DIR=%BUILD_DIR%\%CONFIG%\_deps\re2-src"
set "RE2_BUILD_DIR=%BUILD_DIR%\re2-external"
set "ABSL_CMAKE_DIR=%CD%\%BUILD_DIR%\absl-install\lib\cmake\absl"

if exist "%RE2_SRC_DIR%\CMakeLists.txt" (
    echo [BUILD]   Found re2 source at: %RE2_SRC_DIR%

    cmake -S "%RE2_SRC_DIR%" -B "%RE2_BUILD_DIR%" ^
        -G "Visual Studio 17 2022" ^
        -A x64 ^
        -DCMAKE_BUILD_TYPE=%CONFIG% ^
        -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded ^
        -DBUILD_SHARED_LIBS=OFF ^
        -DRE2_BUILD_TESTING=OFF ^
        -Dabsl_DIR="%ABSL_CMAKE_DIR%"

    if errorlevel 1 (
        echo [BUILD] WARNING: re2 cmake configure failed
    ) else (
        cmake --build "%RE2_BUILD_DIR%" --config %CONFIG%
        if errorlevel 1 (
            echo [BUILD] WARNING: re2 build failed
        ) else (
            if exist "%RE2_BUILD_DIR%\%CONFIG%\re2.lib" (
                if not exist "%STATIC_INSTALL_DIR%\lib" mkdir "%STATIC_INSTALL_DIR%\lib"
                copy /y "%RE2_BUILD_DIR%\%CONFIG%\re2.lib" "%STATIC_INSTALL_DIR%\lib\" >nul
                echo [BUILD]   Copied re2.lib from standalone build
            ) else (
                echo [BUILD] WARNING: re2.lib not found in standalone build output
            )
        )
    )
) else (
    echo [BUILD] WARNING: re2 source not found at %RE2_SRC_DIR%
)

echo [BUILD] Step 1b/6 complete.

REM -------------------------------------------------------------------
REM 2. CMake --install (build.py already built, now install)
REM -------------------------------------------------------------------
echo [BUILD] Step 2/6: cmake --install...

cmake --install "%BUILD_DIR%\%CONFIG%" --config %CONFIG%
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

set libs=
if exist "%TLOG%" (
    echo [BUILD] Tlog found, parsing...
    for /f "delims=" %%L in ('type "%TLOG%"') do (
        set LINE=%%L
        set LINE=!LINE:^=!
        if /i "!LINE:~-4!" == ".LIB" (
            set LINE=!LINE:"=!
            set libs=!libs! !LINE!
        )
    )
    if "!libs!" == "" (
        echo [BUILD] WARNING: No .lib files found in link tlog
    ) else (
        echo [BUILD] Found libs: !libs!
    )
) else (
    echo [BUILD] WARNING: link tlog not found at %TLOG%
)
echo [BUILD] Step 3/6 complete.

REM -------------------------------------------------------------------
REM 4. Merge static libs with lib.exe (only if tlog had libs)
REM -------------------------------------------------------------------
if not exist "%STATIC_INSTALL_DIR%\lib" mkdir "%STATIC_INSTALL_DIR%\lib"

if "!libs!" == "" (
    echo [BUILD] Step 4/6: Skipping lib merge [no libs from tlog]
) else (
    echo [BUILD] Step 4/6: Merging static libs...
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
)
echo [BUILD] Step 4/6 complete.

REM -------------------------------------------------------------------
REM 4b. Collect third-party libs from build tree
REM -------------------------------------------------------------------
echo [BUILD] Step 4b/6: Collecting third-party libs from build tree...

REM The install directory only contains onnxruntime's own libs.
REM Third-party deps (protobuf, re2, onnx, absl, etc.) are in the build tree.
REM Search for all .lib files and copy them to install-static/lib.
for /f "delims=" %%f in ('dir /s /b "%BUILD_DIR%\*.lib" 2^>nul') do (
    echo %%f | findstr /i "\\install\\" >nul
    if errorlevel 1 (
        if not exist "%STATIC_INSTALL_DIR%\lib\%%~nxf" (
            copy /y "%%f" "%STATIC_INSTALL_DIR%\lib\" >nul 2>nul
            if not errorlevel 1 (
                echo [BUILD]   Copied: %%~nxf
            )
        )
    )
)

REM Ensure re2.lib is collected (onnxruntime contrib_ops depend on it)
if not exist "%STATIC_INSTALL_DIR%\lib\re2.lib" (
    echo [BUILD] re2.lib not found in install-static/lib, searching build tree...
    set "RE2_FOUND=0"
    for /f "delims=" %%f in ('dir /s /b "%BUILD_DIR%\re2.lib" 2^>nul') do (
        copy /y "%%f" "%STATIC_INSTALL_DIR%\lib\" >nul 2>nul
        if not errorlevel 1 (
            echo [BUILD]   Copied re2.lib from: %%f
            set "RE2_FOUND=1"
        )
    )
    if "!RE2_FOUND!"=="0" (
        echo [BUILD] WARNING: re2.lib not found in any location - DittoOCR will fail to link
    ) else (
        echo [BUILD] re2.lib copied to install-static/lib
    )
) else (
    echo [BUILD] re2.lib already present in install-static/lib
)
echo [BUILD] Step 4b/6 complete.

REM -------------------------------------------------------------------
REM 4c. Merge ALL libs into a single onnxruntime.lib
REM -------------------------------------------------------------------
echo [BUILD] Step 4c/6: Merging all libs into onnxruntime.lib...

if exist "%STATIC_INSTALL_DIR%\lib\onnxruntime.lib" (
    del "%STATIC_INSTALL_DIR%\lib\onnxruntime.lib"
)

set all_libs=
for /f "delims=" %%f in ('dir /b "%STATIC_INSTALL_DIR%\lib\*.lib" 2^>nul') do (
    set all_libs=!all_libs! "%STATIC_INSTALL_DIR%\lib\%%f"
)

if "!all_libs!" == "" (
    echo [BUILD] WARNING: No libs found in install-static/lib
) else (
    echo [BUILD] Merging all libs into onnxruntime.lib...
    lib.exe /OUT:"%STATIC_INSTALL_DIR%\lib\onnxruntime.lib" !all_libs!
    if errorlevel 1 (
        echo [BUILD] WARNING: Final merge failed
    ) else (
        echo [BUILD] onnxruntime.lib created successfully - all deps included
    )
)
echo [BUILD] Step 4c/6 complete.

REM -------------------------------------------------------------------
REM 4c2. Remove redundant individual libs (merged into onnxruntime.lib)
REM -------------------------------------------------------------------
echo [BUILD] Step 4c2/6: Removing redundant individual libs...

if exist "%STATIC_INSTALL_DIR%\lib\onnxruntime_common.lib" (
    echo [BUILD]   Removing onnxruntime_*.lib (merged into onnxruntime.lib)...
    del "%STATIC_INSTALL_DIR%\lib\onnxruntime_*.lib" 2>nul
)

dir /b "%STATIC_INSTALL_DIR%\lib\absl_*.lib" >nul 2>nul && (
    echo [BUILD]   Removing absl_*.lib files (merged into onnxruntime.lib)...
    del "%STATIC_INSTALL_DIR%\lib\absl_*.lib" 2>nul
)

echo [BUILD] Step 4c2/6 complete.

REM -------------------------------------------------------------------
REM 4d. Verify re2 symbols in onnxruntime.lib
REM -------------------------------------------------------------------
echo [BUILD] Step 4d/6: Verifying re2 symbols in onnxruntime.lib...

if exist "%STATIC_INSTALL_DIR%\lib\onnxruntime.lib" (
    where dumpbin >nul 2>nul
    if errorlevel 1 (
        echo [BUILD] dumpbin not found, skipping re2 verification
    ) else (
        REM Check that re2::RE2 symbols are DEFINED (sec N), not just referenced (sec  0)
        REM dumpbin /symbols outputs (sec  0) for undefined (two spaces), (sec  N) for defined (one space)
        REM Use "sec  0" (two spaces) to match only undefined symbols without using parentheses
        dumpbin /symbols "%STATIC_INSTALL_DIR%\lib\onnxruntime.lib" | findstr /i "re2::RE2" | findstr /v "sec  0" >nul
        if errorlevel 1 (
            echo [BUILD] WARNING: re2::RE2 symbols NOT defined in onnxruntime.lib
            echo [BUILD]   This will cause DittoOCR link failures!
        ) else (
            echo [BUILD] OK: re2::RE2 symbols verified - defined in onnxruntime.lib
        )
    )
) else (
    echo [BUILD] WARNING: onnxruntime.lib not found at %STATIC_INSTALL_DIR%\lib\onnxruntime.lib
)
echo [BUILD] Step 4d/6 complete.

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
    echo endif(^)
    echo add_library(onnxruntime STATIC IMPORTED)
    echo set_target_properties(onnxruntime PROPERTIES
    echo   IMPORTED_LOCATION "${ONNXRUNTIME_LIBRARIES}"
    echo   INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_INCLUDE_DIRS}"
    echo   INTERFACE_COMPILE_DEFINITIONS "ONNXRUNTIME_STATIC_DEFINE"
    echo ^)
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
    echo [BUILD] ONNX Runtime static libs built - individual libs, no merge
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
        xcopy "%INSTALL_DIR%\include" "%STATIC_INSTALL_DIR%\include" /s /e /y /i >nul
    )
    echo [BUILD] ===================================================================
    echo [BUILD] ONNX Runtime packaged from install dir - fallback
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
