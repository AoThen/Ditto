@echo off
set MODELS_DIR=%~dp0models
set ZIP_URL=https://github.com/AoThen/Ditto/releases/download/PP-OCRv6/pp-ocr-v6-small-models.zip

if not exist "%MODELS_DIR%" mkdir "%MODELS_DIR%"

if not exist "%MODELS_DIR%\PP-OCRv6_small_det_infer.onnx" (
    echo Downloading OCR models from GitHub Releases...
    powershell -Command "Invoke-WebRequest -Uri '%ZIP_URL%' -OutFile '%TEMP%\ocr_models.zip'"
    powershell -Command "Expand-Archive -Path '%TEMP%\ocr_models.zip' -DestinationPath '%MODELS_DIR%' -Force"
    del "%TEMP%\ocr_models.zip"
    echo Done.
) else (
    echo OCR models already exist, skipping download.
)