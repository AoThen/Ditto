@echo off
set OCR_DIR=%~dp0..\Addins\DittoOCR
set RAPID_DIR=%OCR_DIR%\rapidocr_onnx

where 7z >nul 2>nul
if errorlevel 1 (
    echo 7-Zip not found in PATH.
    exit /b 1
)

REM 1. 下载 ONNX Runtime 静态库（如果不存在，v1.23.2）
if not exist "%RAPID_DIR%\onnxruntime-static\windows-x64" (
    echo Downloading ONNX Runtime static libs...
    if not exist "%RAPID_DIR%" mkdir "%RAPID_DIR%"
    powershell -Command "Invoke-WebRequest -Uri 'https://github.com/AoThen/Ditto/releases/download/onnxruntime-1.23.2/onnxruntime-static-windows-x64-1.23.2.7z' -OutFile '%TEMP%\onnx.7z'"
    7z t "%TEMP%\onnx.7z" >nul
    if errorlevel 1 (
        echo ONNX Runtime archive is corrupted, aborting
        del "%TEMP%\onnx.7z"
        exit /b 1
    )
    REM 解压到临时目录，再移动到 windows-x64，兼容不同版本归档的目录结构
    7z x "%TEMP%\onnx.7z" -o"%RAPID_DIR%\onnxruntime-static\tmp" -y
    if exist "%RAPID_DIR%\onnxruntime-static\tmp\windows-x64" (
        move "%RAPID_DIR%\onnxruntime-static\tmp\windows-x64" "%RAPID_DIR%\onnxruntime-static\"
    ) else (
        mkdir "%RAPID_DIR%\onnxruntime-static\windows-x64"
        for /d %%i in ("%RAPID_DIR%\onnxruntime-static\tmp\*") do (
            move "%%i" "%RAPID_DIR%\onnxruntime-static\windows-x64\"
        )
    )
    rmdir /s /q "%RAPID_DIR%\onnxruntime-static\tmp"
    del "%TEMP%\onnx.7z"
    if not exist "%RAPID_DIR%\onnxruntime-static\windows-x64" (
        echo ERROR: ONNX Runtime extraction failed - windows-x64 directory not found
        exit /b 1
    )
    REM 兼容不同版本：headers 可能在 include\ 根目录（旧版）或 include\onnxruntime\core\session\（新版）
    if not exist "%RAPID_DIR%\onnxruntime-static\windows-x64\include\onnxruntime_cxx_api.h" (
        if exist "%RAPID_DIR%\onnxruntime-static\windows-x64\include\onnxruntime\core\session\onnxruntime_cxx_api.h" (
            copy "%RAPID_DIR%\onnxruntime-static\windows-x64\include\onnxruntime\core\session\*.h" "%RAPID_DIR%\onnxruntime-static\windows-x64\include\"
        )
    )
)

REM 2. 下载 OpenCV 静态库（如果不存在）
if not exist "%RAPID_DIR%\opencv-static\windows-x64" (
    echo Downloading OpenCV static libs...
    powershell -Command "Invoke-WebRequest -Uri 'https://github.com/AoThen/Ditto/releases/download/opencv-4.8.1/opencv-static-windows-x64-4.8.1.7z' -OutFile '%TEMP%\opencv.7z'"
    7z t "%TEMP%\opencv.7z" >nul
    if errorlevel 1 (
        echo OpenCV archive is corrupted, aborting
        del "%TEMP%\opencv.7z"
        exit /b 1
    )
    7z x "%TEMP%\opencv.7z" -o"%RAPID_DIR%\opencv-static" -y
    del "%TEMP%\opencv.7z"
)

REM 3. 下载 PP-OCRv6_small 模型
call "%OCR_DIR%\download-models.bat"

echo Dependency setup complete.