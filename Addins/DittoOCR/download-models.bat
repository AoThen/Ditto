@echo off
set MODELS_URL=https://github.com/RapidAI/RapidOcrOnnx/releases/download/init/models.7z
set MODELS_FILE=models.7z
set MODELS_DIR=models

if not exist "%MODELS_DIR%" mkdir "%MODELS_DIR%"
echo Downloading OCR models from %MODELS_URL%...
powershell -Command "& {Invoke-WebRequest -Uri '%MODELS_URL%' -OutFile '%MODELS_FILE%'}"
echo Extracting models...
powershell -Command "& {Expand-7Zip -ArchiveFileName '%MODELS_FILE%' -TargetPath '%MODELS_DIR%'}"
del "%MODELS_FILE%"
echo Done.