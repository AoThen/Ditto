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
    powershell -Command "Invoke-WebRequest -Uri 'https://github.com/RapidAI/OnnxruntimeBuilder/releases/download/1.23.2/onnxruntime-v1.23.2-windows-vs2022-x64-static-mt.7z' -OutFile '%TEMP%\onnx.7z'"
    7z t "%TEMP%\onnx.7z" >nul
    if errorlevel 1 (
        echo ONNX Runtime archive is corrupted, aborting
        del "%TEMP%\onnx.7z"
        exit /b 1
    )
    7z x "%TEMP%\onnx.7z" -o"%RAPID_DIR%\onnxruntime-static" -y
    del "%TEMP%\onnx.7z"
    if not exist "%RAPID_DIR%\onnxruntime-static\windows-x64\include\onnxruntime\core\session" (
        mkdir "%RAPID_DIR%\onnxruntime-static\windows-x64\include\onnxruntime\core\session"
        copy "%RAPID_DIR%\onnxruntime-static\windows-x64\include\*.h" "%RAPID_DIR%\onnxruntime-static\windows-x64\include\onnxruntime\core\session\"
    )
)

REM 2. 下载 OpenCV 静态库（如果不存在）
if not exist "%RAPID_DIR%\opencv-static\windows-x64" (
    echo Downloading OpenCV static libs...
    powershell -Command "Invoke-WebRequest -Uri 'https://github.com/RapidAI/OpenCVBuilder/releases/download/4.8.1/opencv-4.8.1-windows-vs2022-mt.7z' -OutFile '%TEMP%\opencv.7z'"
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