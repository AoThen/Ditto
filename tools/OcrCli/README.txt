========================================
 OcrCli — Ditto OCR 命令行工具
========================================

用法:
  OcrCli.exe <image1> [image2 ...] [选项]

选项:
  -m <dir>    模型目录 (默认: <exe_dir>/models)
  -o <file>   输出到 UTF-8 文件 (默认: stdout)
  -d <path>   OcrDll.dll 路径 (默认: <exe_dir>/OcrDll.dll)
  -j, --json  以 JSON 数组格式输出 (用于程序化调用)
  -q, --quiet 安静模式，抑制 stderr 诊断信息

环境变量:
  OCR_CLI_MODELS_DIR  默认模型目录 (优先级: CLI > 环境变量 > 默认)
  OCR_CLI_DLL_PATH    默认 OcrDll.dll 路径 (优先级: CLI > 环境变量 > 默认)

示例:
  OcrCli.exe screenshot.png
  OcrCli.exe img1.png img2.png -o result.txt
  OcrCli.exe img.png -j
  OcrCli.exe img.png -j -q > result.json

返回值:
  0  全部成功
  1  部分或全部失败

文件清单:
  OcrCli.exe      — 主程序
  OcrDll.dll      — OCR 引擎 DLL
  models/         — PP-OCRv6 模型文件 (3 个)
  README.txt      — 本说明