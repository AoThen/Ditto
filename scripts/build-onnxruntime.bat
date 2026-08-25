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
echo [BUILD] Step 1/6: Running build.py (configure + build)

python tools\ci_build\build.py ^
    --build_dir %BUILD_DIR% ^
    --config %CONFIG% ^
    --parallel ^
    --skip_tests ^
    --cmake_generator "Visual Studio 17 2022" ^
    --enable_msvc_static_runtime ^
    --cmake_extra_defines ^
        "CMAKE_INSTALL_PREFIX=install" ^
        "onnxruntime_BUILD_UNIT_TESTS=OFF" ^
        "onnxruntime_ENABLE_PYTHON=OFF" ^
        "CMAKE_MSVC_RUNTIME_LIBRARY:STRING=MultiThreaded" ^
        "ABSL_MSVC_STATIC_RUNTIME=ON"

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

echo [BUILD] Step 1a/6: Building absl from source

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
        -DABSL_MSVC_STATIC_RUNTIME=ON ^
        -DBUILD_SHARED_LIBS=OFF ^
        -DABSL_BUILD_TESTING=OFF

    if errorlevel 1 (
        echo [BUILD] ERROR: absl cmake configure failed - cannot provide /MT absl libs
        endlocal & exit /b 1
    ) else (
        cmake --build "%ABSL_BUILD_DIR%" --config %CONFIG%
        if errorlevel 1 (
            echo [BUILD] ERROR: absl build failed - cannot provide /MT absl libs
            endlocal & exit /b 1
        ) else (
            echo [BUILD]   absl built successfully
            cmake --install "%ABSL_BUILD_DIR%" --config %CONFIG% --prefix "%ABSL_INSTALL_DIR%"
            if errorlevel 1 (
                echo [BUILD] ERROR: absl install failed - cannot provide /MT absl libs
                endlocal & exit /b 1
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

echo [BUILD] Step 1b/6: Building re2 as standalone static library

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
        -DCMAKE_POLICY_DEFAULT_CMP0091=NEW ^
        -DBUILD_SHARED_LIBS=OFF ^
        -DRE2_BUILD_TESTING=OFF ^
        -Dabsl_DIR="%ABSL_CMAKE_DIR%"

    if errorlevel 1 (
        echo [BUILD] ERROR: re2 cmake configure failed - DittoOCR will fail to link
        endlocal & exit /b 1
    ) else (
        cmake --build "%RE2_BUILD_DIR%" --config %CONFIG%
        if errorlevel 1 (
            echo [BUILD] ERROR: re2 build failed - DittoOCR will fail to link
            endlocal & exit /b 1
        ) else (
            if exist "%RE2_BUILD_DIR%\%CONFIG%\re2.lib" (
                if exist "%STATIC_INSTALL_DIR%\lib" (
                    rem dir exists, skip
                ) else (
                    mkdir "%STATIC_INSTALL_DIR%\lib"
                )
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
echo [BUILD] Step 2/6: cmake --install

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
REM 3. Copy ORT core libs from install dir into merge staging area
REM    (cmake --install outputs onnxruntime_*.lib split static libs;
REM     the old tlog-parsing approach never worked due to UTF-16 and
REM     is removed - without this step the merged lib would lack
REM     core symbols like OrtGetApiBase)
REM -------------------------------------------------------------------
if exist "%STATIC_INSTALL_DIR%\lib" (rem) else mkdir "%STATIC_INSTALL_DIR%\lib"

echo [BUILD] Step 3/6: Copying ORT core libs from install dir

if exist "%INSTALL_DIR%\lib\*.lib" (
    xcopy "%INSTALL_DIR%\lib\*.lib" "%STATIC_INSTALL_DIR%\lib\" /y >nul
    echo [BUILD]   Copied ORT core libs from %INSTALL_DIR%\lib:
    dir /b "%INSTALL_DIR%\lib\*.lib" 2>nul
) else (
    echo [BUILD] WARNING: no .lib files at %INSTALL_DIR%\lib - merged lib would lack ORT core symbols
)

echo [BUILD] Step 3.5/6: Copying ORT headers from install dir

if exist "%INSTALL_DIR%\include" (
    xcopy "%INSTALL_DIR%\include" "%STATIC_INSTALL_DIR%\include\" /s /e /y /i >nul
    echo [BUILD]   Copied headers from %INSTALL_DIR%\include
) else (
    echo [BUILD] ERROR: no include dir at %INSTALL_DIR%\include - consumers cannot compile
)
echo [BUILD] Step 3/6 complete.

REM -------------------------------------------------------------------
REM 4b. Collect third-party libs from build tree
REM -------------------------------------------------------------------
echo [BUILD] Step 4b/6: Collecting third-party libs from build tree

REM The install directory only contains onnxruntime's own libs.
REM Third-party deps (protobuf, re2, onnx, absl, etc.) are in the build tree.
REM Search for all .lib files and copy them to install-static/lib.
for /f "delims=" %%f in ('dir /s /b "%BUILD_DIR%\*.lib" 2^>nul') do (
    echo %%f | findstr /i "\\install\\" >nul
    if errorlevel 1 (
        echo %%f | findstr /i "\\_deps\\" >nul
        if errorlevel 1 (
            if exist "%STATIC_INSTALL_DIR%\lib\%%~nxf" (
                rem file already exists, skip
            ) else (
                copy /y "%%f" "%STATIC_INSTALL_DIR%\lib\" >nul 2>nul
                if errorlevel 1 (
                    rem copy failed, continue
                ) else (
                    echo [BUILD]   Copied: %%~nxf
                )
            )
        )
    )
)

REM Ensure re2.lib is collected (onnxruntime contrib_ops depend on it)
if exist "%STATIC_INSTALL_DIR%\lib\re2.lib" (
    echo [BUILD] re2.lib already present in install-static/lib
) else (
    echo [BUILD] re2.lib not found in install-static/lib, searching build tree
    set "RE2_FOUND=0"
    for /f "delims=" %%f in ('dir /s /b "%BUILD_DIR%\re2.lib" 2^>nul') do (
        copy /y "%%f" "%STATIC_INSTALL_DIR%\lib\" >nul 2>nul
        if errorlevel 1 (
            rem copy failed, continue
        ) else (
            echo [BUILD]   Copied re2.lib from: %%f
            set "RE2_FOUND=1"
        )
    )
    if "!RE2_FOUND!"=="0" (
        echo [BUILD] WARNING: re2.lib not found in any location - DittoOCR will fail to link
    ) else (
        echo [BUILD] re2.lib copied to install-static/lib
    )
)
REM Force re2.lib from standalone build (overrides any ONNX Runtime internal build copy)
if exist "%RE2_BUILD_DIR%\%CONFIG%\re2.lib" (
    copy /y "%RE2_BUILD_DIR%\%CONFIG%\re2.lib" "%STATIC_INSTALL_DIR%\lib\re2.lib" >nul
    if errorlevel 1 (
        echo [BUILD] WARNING: Failed to copy re2.lib from standalone build
    ) else (
        echo [BUILD]   Forced re2.lib from standalone build
    )
) else (
    echo [BUILD] WARNING: Standalone re2.lib not found at %RE2_BUILD_DIR%\%CONFIG%\re2.lib
)
echo [BUILD] Step 4b/6 complete.

REM -------------------------------------------------------------------
REM 4b2. Override absl libs with standalone build versions
REM -------------------------------------------------------------------
REM The generic search in Step 4b may have picked up absl libs from the
REM FetchContent build tree (compiled with /MD). Override them with the
REM standalone build versions (compiled with /MT via ABSL_MSVC_STATIC_RUNTIME).
if exist "%ABSL_BUILD_DIR%" (
    echo [BUILD] Step 4b2/6: Overriding absl libs with standalone build versions
    set "ABSL_OVERRIDDEN=0"
    for /f "delims=" %%f in ('dir /s /b "%ABSL_BUILD_DIR%\absl_*.lib" 2^>nul') do (
        copy /y "%%f" "%STATIC_INSTALL_DIR%\lib\" >nul
        if errorlevel 1 (
            rem copy failed, continue
        ) else (
            set "ABSL_OVERRIDDEN=1"
            echo [BUILD]   Overrode: %%~nxf
        )
    )
    if "!ABSL_OVERRIDDEN!"=="0" (
        echo [BUILD] WARNING: No absl libs found in standalone build tree - CRT mismatch likely
    )
    echo [BUILD] Step 4b2/6 complete.
) else (
    echo [BUILD] WARNING: ABSL_BUILD_DIR not found, skipping absl lib override
)

REM -------------------------------------------------------------------
REM 4b3. Diagnostic: check each individual lib for MSVCRT references
REM -------------------------------------------------------------------
echo [BUILD] Step 4b3/6: Checking individual libs for MSVCRT references

if exist "%STATIC_INSTALL_DIR%\lib" (
    where dumpbin >nul 2>nul
    if errorlevel 1 (
        echo [BUILD] WARNING: dumpbin not found, skipping individual lib CRT check
    ) else (
        echo [BUILD]   Checking each .lib file for MSVCRT references...
        for /f "delims=" %%f in ('dir /b "%STATIC_INSTALL_DIR%\lib\*.lib" 2^>nul') do (
            dumpbin /directives "%STATIC_INSTALL_DIR%\lib\%%f" > "%TEMP%\crt_check.txt"
            findstr /i "DEFAULTLIB:MSVCRT" "%TEMP%\crt_check.txt" >nul
            if errorlevel 1 (
                rem no MSVCRT in this lib
            ) else (
                echo [BUILD]   [MSVCRT] %%f
                for /f "tokens=*" %%a in ('findstr /i "DEFAULTLIB:MSVCRT" "%TEMP%\crt_check.txt"') do echo [BUILD]       %%a
            )
            del "%TEMP%\crt_check.txt" 2>nul
        )
        echo [BUILD]   Individual lib CRT check complete
    )
) else (
    echo [BUILD] WARNING: install-static/lib not found
)
echo [BUILD] Step 4b3/6 complete.

REM -------------------------------------------------------------------
REM 4c. Merge ALL libs into a single onnxruntime.lib
REM -------------------------------------------------------------------
echo [BUILD] Step 4c/6: Merging all libs into onnxruntime.lib

if exist "%STATIC_INSTALL_DIR%\lib\onnxruntime.lib" (
    del "%STATIC_INSTALL_DIR%\lib\onnxruntime.lib"
)

set all_libs=
for /f "delims=" %%f in ('dir /b "%STATIC_INSTALL_DIR%\lib\*.lib" 2^>nul') do (
    if /i "%%f"=="onnxruntime.lib" (
        rem skip onnxruntime.lib itself
    ) else (
        set all_libs=!all_libs! "%STATIC_INSTALL_DIR%\lib\%%f"
    )
)

if "!all_libs!" == "" (
    echo [BUILD] WARNING: No libs found in install-static/lib
) else (
    echo [BUILD] Merging all libs into onnxruntime.lib
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
echo [BUILD] Step 4c2/6: Removing redundant individual libs

if exist "%STATIC_INSTALL_DIR%\lib\onnxruntime_common.lib" (
    echo [BUILD]   Removing onnxruntime_*.lib (merged into onnxruntime.lib)
    del "%STATIC_INSTALL_DIR%\lib\onnxruntime_*.lib" 2>nul
)

if exist "%STATIC_INSTALL_DIR%\lib\absl_base.lib" (
    echo [BUILD]   Removing absl_*.lib files (merged into onnxruntime.lib)
    del "%STATIC_INSTALL_DIR%\lib\absl_*.lib" 2>nul
)

echo [BUILD] Step 4c2/6 complete.

REM -------------------------------------------------------------------
REM 4d. Verify re2 member objects in onnxruntime.lib
REM -------------------------------------------------------------------
echo [BUILD] Step 4d/6: Verifying re2 member objects in onnxruntime.lib

if exist "%STATIC_INSTALL_DIR%\lib\onnxruntime.lib" (
    where dumpbin >nul 2>nul
    if errorlevel 1 (
        echo [BUILD] dumpbin not found, skipping re2 verification
    ) else (
        REM Use /linkermember:1 to list member objects instead of /symbols
        REM (avoids FINDSTR: Line X is too long issues with huge symbol tables)
        dumpbin /linkermember:1 "%STATIC_INSTALL_DIR%\lib\onnxruntime.lib" > "%TEMP%\onnx_re2_members.txt"
        findstr /i "re2" "%TEMP%\onnx_re2_members.txt" >nul
        if errorlevel 1 (
            echo [BUILD] WARNING: re2 member objects NOT found in onnxruntime.lib
            echo [BUILD]   This will cause DittoOCR link failures!
        ) else (
            echo [BUILD] OK: re2 member objects found in onnxruntime.lib
        )
        del "%TEMP%\onnx_re2_members.txt" 2>nul
    )
) else (
    echo [BUILD] WARNING: onnxruntime.lib not found at %STATIC_INSTALL_DIR%\lib\onnxruntime.lib
)
echo [BUILD] Step 4d/6 complete.

call :verify_crt
if errorlevel 1 exit /b 1
echo [BUILD] Step 4e/6 complete.

REM -------------------------------------------------------------------
REM 5. Flatten headers
REM -------------------------------------------------------------------
echo [BUILD] Step 5/6: Flattening headers

if exist "%STATIC_INSTALL_DIR%\include" (rem) else mkdir "%STATIC_INSTALL_DIR%\include"

if exist "%INSTALL_DIR%\include\onnxruntime" (
    echo [BUILD] Copying onnxruntime headers
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
echo [BUILD] Step 6/6: Creating cmake config

if exist "%STATIC_INSTALL_DIR%\lib\cmake\OnnxRuntime" (rem) else mkdir "%STATIC_INSTALL_DIR%\lib\cmake\OnnxRuntime"
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
if errorlevel 1 (
    rem dir command failed, no libs found
) else (
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

REM -------------------------------------------------------------------
REM Subroutine: report MSVCRT found in onnxruntime.lib and exit with error
REM Flat structure - no for loops, no delayed expansion, no nested blocks
REM -------------------------------------------------------------------
:report_msvcrt_found
echo [BUILD]   MSVCRT references found in onnxruntime.lib:
findstr /i "DEFAULTLIB:MSVCRT" "%TEMP%\onnx_crt_directives.txt"
echo [BUILD] ERROR: onnxruntime.lib contains MSVCRT references - /MD detected
echo [BUILD]   This will cause LNK2038 mismatch with DittoOCR - expects /MT
del "%TEMP%\onnx_crt_directives.txt" "%TEMP%\msvcrt_matches.txt" 2>nul
exit /b 1

REM -------------------------------------------------------------------
REM Verify CRT subroutine
REM Flat goto structure: no nested if/else blocks, no & chains, and all
REM echo messages are free of unescaped parentheses - a ")" inside a
REM parenthesized block prematurely closes it and causes the fatal
REM "not was unexpected at this time." parse error.
REM -------------------------------------------------------------------
:verify_crt
echo [BUILD] Step 4e/6: Verifying CRT in onnxruntime.lib

if exist "%STATIC_INSTALL_DIR%\lib\onnxruntime.lib" goto verify_crt_have_lib
echo [BUILD] WARNING: onnxruntime.lib not found, skipping CRT verification
exit /b 0

:verify_crt_have_lib
where dumpbin >nul 2>nul
if errorlevel 1 goto verify_crt_no_dumpbin

dumpbin /directives "%STATIC_INSTALL_DIR%\lib\onnxruntime.lib" > "%TEMP%\onnx_crt_directives.txt"

rem MSVCRT directive means dynamic CRT /MD - fatal for DittoOCR /MT link
findstr /i "DEFAULTLIB:MSVCRT" "%TEMP%\onnx_crt_directives.txt" > "%TEMP%\msvcrt_matches.txt"
if errorlevel 1 goto verify_crt_check_libcmt
call :report_msvcrt_found
exit /b 1

:verify_crt_check_libcmt
findstr /i "DEFAULTLIB:LIBCMT" "%TEMP%\onnx_crt_directives.txt" >nul
if errorlevel 1 goto verify_crt_no_libcmt
echo [BUILD] OK: onnxruntime.lib uses static CRT LIBCMT - /MT confirmed
goto verify_crt_cleanup

:verify_crt_no_libcmt
echo [BUILD] WARNING: LIBCMT static CRT marker not found in onnxruntime.lib
goto verify_crt_cleanup

:verify_crt_cleanup
del "%TEMP%\onnx_crt_directives.txt" "%TEMP%\msvcrt_matches.txt" 2>nul
exit /b 0

:verify_crt_no_dumpbin
echo [BUILD] WARNING: dumpbin not found, skipping CRT verification
exit /b 0
