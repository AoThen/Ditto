@echo off
set RAPID_DIR=%~dp0..\Addins\DittoOCR\rapidocr_onnx
set BUILD_DIR=%RAPID_DIR%\build

where 7z >nul 2>nul
if errorlevel 1 (
    echo 7-Zip not found in PATH. Please install 7-Zip or add it to PATH.
    exit /b 1
)

if not exist "%RAPID_DIR%\CMakeLists.txt" (
    echo RapidOcrOnnx source not found at %RAPID_DIR%
    exit /b 1
)

REM 下载 ONNX Runtime 静态库（如果不存在）
if not exist "%RAPID_DIR%\onnxruntime-static\windows-x64" (
    echo Downloading ONNX Runtime static libs...
    powershell -Command "Invoke-WebRequest -Uri 'https://github.com/RapidAI/OnnxruntimeBuilder/releases/download/1.15.1/onnxruntime-v1.15.1-windows-vs2022-static-mt.7z' -OutFile '%TEMP%\onnx.7z'"
    7z t "%TEMP%\onnx.7z" >nul
    if errorlevel 1 (
        echo ONNX Runtime archive is corrupted, aborting
        del "%TEMP%\onnx.7z"
        exit /b 1
    )
    7z x "%TEMP%\onnx.7z" -o"%RAPID_DIR%\onnxruntime-static" -y
    del "%TEMP%\onnx.7z"
)

REM 下载 OpenCV 静态库（如果不存在）
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

REM CMake 构建 CLIB 目标
echo Configuring CMake...
cmake -S "%RAPID_DIR%" -B "%BUILD_DIR%" ^
    -T "v143,host=x64" -A "x64" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DOCR_OUTPUT="CLIB" ^
    -DOCR_BUILD_CRT="True" ^
    -DOCR_ONNX="CPU" ^
    -DOpenCV_RUNTIME=vc17 -DOpenCV_ARCH=x64

echo Building RapidOcrOnnx...
cmake --build "%BUILD_DIR%" --config Release -j %NUMBER_OF_PROCESSORS%

echo Build complete. Output: %BUILD_DIR%\Release\RapidOcrOnnx.dll