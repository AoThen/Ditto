@echo off
set OCR_DIR=%~dp0..\Addins\DittoOCR
set DEPS_DIR=%OCR_DIR%\ocr-deps

where 7z >nul 2>nul
if errorlevel 1 (
    echo 7-Zip not found in PATH.
    exit /b 1
)

REM === 版本选择 ===
set OCR_VERSION=new
if /I "%~1"=="legacy" set OCR_VERSION=legacy

if "%OCR_VERSION%"=="legacy" (
    set ONNX_VER=1.23.2
    set ONNX_DIR=onnxruntime-1232-static
    set OPENCV_VER=4.8.1
    set OPENCV_DIR=opencv-481-static
) else (
    set ONNX_VER=1.27.1
    set ONNX_DIR=onnxruntime-static
    set OPENCV_VER=4.14.0
    set OPENCV_DIR=opencv-static
)

REM 1. 下载 ONNX Runtime（如果不存在）
if not exist "%DEPS_DIR%\%ONNX_DIR%\windows-x64" (
    echo Downloading ONNX Runtime v%ONNX_VER%...
    if not exist "%DEPS_DIR%" mkdir "%DEPS_DIR%"
    REM Download + integrity test with up to 3 attempts (flaky CDN truncation)
    powershell -Command "$ProgressPreference='SilentlyContinue'; $u='https://github.com/AoThen/Ditto/releases/download/onnxruntime-%ONNX_VER%/onnxruntime-static-windows-x64-%ONNX_VER%.7z'; $o='%TEMP%\onnx.7z'; foreach($i in 1..3){ try { Invoke-WebRequest -Uri $u -OutFile $o -ErrorAction Stop; & 7z t $o *> $null; if ($LASTEXITCODE -eq 0) { exit 0 } } catch { }; Remove-Item $o -ErrorAction SilentlyContinue; Start-Sleep -Seconds 5 }; exit 1"
    if errorlevel 1 (
        echo ONNX Runtime download failed integrity check after 3 attempts, aborting
        del "%TEMP%\onnx.7z" 2>nul
        exit /b 1
    )
    REM 解压到临时目录，再移动到 windows-x64，兼容不同版本归档的目录结构
    7z x "%TEMP%\onnx.7z" -o"%DEPS_DIR%\%ONNX_DIR%\tmp" -y
    if exist "%DEPS_DIR%\%ONNX_DIR%\tmp\windows-x64" (
        move "%DEPS_DIR%\%ONNX_DIR%\tmp\windows-x64" "%DEPS_DIR%\%ONNX_DIR%\"
    ) else (
        mkdir "%DEPS_DIR%\%ONNX_DIR%\windows-x64"
        xcopy "%DEPS_DIR%\%ONNX_DIR%\tmp\*" "%DEPS_DIR%\%ONNX_DIR%\windows-x64\" /s /e /y /i >nul 2>nul
    )
    rmdir /s /q "%DEPS_DIR%\%ONNX_DIR%\tmp"
    del "%TEMP%\onnx.7z"
    if not exist "%DEPS_DIR%\%ONNX_DIR%\windows-x64" (
        echo ERROR: ONNX Runtime extraction failed - windows-x64 directory not found
        exit /b 1
    )
    REM 兼容不同版本：headers 可能在 include\ 根目录（旧版）或 include\onnxruntime\core\session\（新版）
    if not exist "%DEPS_DIR%\%ONNX_DIR%\windows-x64\include\onnxruntime_cxx_api.h" (
        if exist "%DEPS_DIR%\%ONNX_DIR%\windows-x64\include\onnxruntime\core\session\onnxruntime_cxx_api.h" (
            copy "%DEPS_DIR%\%ONNX_DIR%\windows-x64\include\onnxruntime\core\session\*.h" "%DEPS_DIR%\%ONNX_DIR%\windows-x64\include\"
        )
    )

    REM 合并拆分库为 onnxruntime.lib，兼容 install 回退归档
    pushd "%DEPS_DIR%\%ONNX_DIR%\windows-x64\lib"
    if not exist "onnxruntime.lib" (
        if exist "onnxruntime_common.lib" (
            echo Merging ONNX Runtime split libs into onnxruntime.lib...
            for /f "usebackq delims=" %%i in (`vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do (
                call "%%i\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
            )
            where lib.exe >nul 2>nul
            if not errorlevel 1 (
                echo Found lib.exe, merging...
                lib.exe /OUT:onnxruntime.lib onnxruntime_*.lib
                if errorlevel 1 (
                    echo WARNING: lib.exe merge failed
                ) else (
                    echo onnxruntime.lib created successfully
                )
            ) else (
                echo WARNING: lib.exe not found, skip merge
                echo ^(DittoOCR build will fail with LNK1104 unless onnxruntime.lib exists^)
            )
        )
    )
    popd
)

REM 2. 下载 OpenCV（如果不存在）
if not exist "%DEPS_DIR%\%OPENCV_DIR%\windows-x64" (
    echo Downloading OpenCV v%OPENCV_VER%...
    powershell -Command "$ProgressPreference='SilentlyContinue'; $u='https://github.com/AoThen/Ditto/releases/download/opencv-%OPENCV_VER%/opencv-static-windows-x64-%OPENCV_VER%.7z'; $o='%TEMP%\opencv.7z'; foreach($i in 1..3){ try { Invoke-WebRequest -Uri $u -OutFile $o -ErrorAction Stop; & 7z t $o *> $null; if ($LASTEXITCODE -eq 0) { exit 0 } } catch { }; Remove-Item $o -ErrorAction SilentlyContinue; Start-Sleep -Seconds 5 }; exit 1"
    if errorlevel 1 (
        echo OpenCV download failed integrity check after 3 attempts, aborting
        del "%TEMP%\opencv.7z" 2>nul
        exit /b 1
    )
    7z x "%TEMP%\opencv.7z" -o"%DEPS_DIR%\%OPENCV_DIR%" -y
    del "%TEMP%\opencv.7z"
)

REM 3. 下载 PP-OCRv6_small 模型
call "%OCR_DIR%\download-models.bat"

REM 4. 诊断：验证提取的目录结构
echo.
echo === Diagnostic: verify extracted files ===
if exist "%DEPS_DIR%\%ONNX_DIR%\windows-x64" (
    echo [OK] ONNX Runtime directory found
    dir "%DEPS_DIR%\%ONNX_DIR%\windows-x64"
    if exist "%DEPS_DIR%\%ONNX_DIR%\windows-x64\include\onnxruntime_cxx_api.h" (
        echo [OK] ONNX Runtime header found
    ) else (
        echo [WARN] ONNX Runtime header NOT found at include\onnxruntime_cxx_api.h
        dir "%DEPS_DIR%\%ONNX_DIR%\windows-x64\include" /s 2>nul
    )
    if exist "%DEPS_DIR%\%ONNX_DIR%\windows-x64\lib\*.lib" (
        echo [OK] ONNX Runtime lib found
    ) else (
        echo [WARN] ONNX Runtime lib NOT found
        dir "%DEPS_DIR%\%ONNX_DIR%\windows-x64\lib" 2>nul
    )
) else (
    echo [WARN] ONNX Runtime directory NOT found
)
if exist "%DEPS_DIR%\%OPENCV_DIR%\windows-x64" (
    echo [OK] OpenCV directory found
    dir "%DEPS_DIR%\%OPENCV_DIR%\windows-x64"
    if exist "%DEPS_DIR%\%OPENCV_DIR%\windows-x64\include\opencv2\opencv.hpp" (
        echo [OK] OpenCV header found
    ) else (
        echo [WARN] OpenCV header NOT found
        dir "%DEPS_DIR%\%OPENCV_DIR%\windows-x64\include" /s 2>nul
    )
    if exist "%DEPS_DIR%\%OPENCV_DIR%\windows-x64\x64\vc17\staticlib\*.lib" (
        echo [OK] OpenCV lib found
    ) else (
        echo [WARN] OpenCV lib NOT found
        dir "%DEPS_DIR%\%OPENCV_DIR%\windows-x64" /s /b *.lib 2>nul
    )
) else (
    echo [WARN] OpenCV directory NOT found
)
echo === Diagnostic end ===
exit /b 0
