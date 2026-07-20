@echo off
set MODELS_DIR=%~dp0models
set DET_URL=https://huggingface.co/PaddlePaddle/PP-OCRv6_small_det_onnx/resolve/main/inference.onnx
set REC_URL=https://huggingface.co/PaddlePaddle/PP-OCRv6_small_rec_onnx/resolve/main/inference.onnx
set REC_CONFIG_URL=https://huggingface.co/PaddlePaddle/PP-OCRv6_small_rec_onnx/resolve/main/inference.yml

if not exist "%MODELS_DIR%" mkdir "%MODELS_DIR%"

echo Downloading PP-OCRv6_small detection model...
powershell -Command "Invoke-WebRequest -Uri '%DET_URL%' -OutFile '%MODELS_DIR%\PP-OCRv6_small_det_infer.onnx'"

echo Downloading PP-OCRv6_small recognition model...
powershell -Command "Invoke-WebRequest -Uri '%REC_URL%' -OutFile '%MODELS_DIR%\PP-OCRv6_small_rec_infer.onnx'"

echo Downloading recognition config...
powershell -Command "Invoke-WebRequest -Uri '%REC_CONFIG_URL%' -OutFile '%MODELS_DIR%\inference.yml'"

echo Done.