#include <windows.h>
#include <gdiplus.h>
#include <cstdio>
#include <string>
#include <vector>

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

struct OcrDll {
    HMODULE module = nullptr;
    void* (*OcrInit)(const char* modelsDir) = nullptr;
    char* (*OcrRecognize)(void* handle, const unsigned char* imageData,
                          int width, int height, int stride) = nullptr;
    void  (*OcrFreeString)(char* str) = nullptr;
    void  (*OcrDestroy)(void* handle) = nullptr;
};

static bool LoadOcrDll(const wchar_t* dllPath, OcrDll& dll, bool quiet = false)
{
    dll.module = LoadLibraryW(dllPath);
    if (!dll.module) {
        if (!quiet) fprintf(stderr, "Error: Failed to load %ws\n", dllPath);
        return false;
    }
    dll.OcrInit = (void* (*)(const char*))GetProcAddress(dll.module, "OcrInit");
    dll.OcrRecognize = (char* (*)(void*, const unsigned char*, int, int, int))
        GetProcAddress(dll.module, "OcrRecognize");
    dll.OcrFreeString = (void (*)(char*))GetProcAddress(dll.module, "OcrFreeString");
    dll.OcrDestroy = (void (*)(void*))GetProcAddress(dll.module, "OcrDestroy");
    if (!dll.OcrInit || !dll.OcrRecognize || !dll.OcrFreeString || !dll.OcrDestroy) {
        if (!quiet) fprintf(stderr, "Error: Failed to locate OCR functions in %ws\n", dllPath);
        FreeLibrary(dll.module);
        dll.module = nullptr;
        return false;
    }
    return true;
}

static std::wstring GetExeDir()
{
    DWORD len = GetModuleFileNameW(nullptr, nullptr, 0);
    if (len == 0) return L".";
    std::wstring path(len, L'\0');
    GetModuleFileNameW(nullptr, path.data(), len);
    path.resize(wcslen(path.c_str()));
    size_t pos = path.rfind(L'\\');
    if (pos != std::wstring::npos)
        path.resize(pos);
    return path;
}

static bool ReadImageToBGRA(const wchar_t* path, std::vector<unsigned char>& pixels,
                            int& w, int& h, int& stride)
{
    Bitmap bmp(path);
    if (bmp.GetLastStatus() != Ok)
        return false;

    w = bmp.GetWidth();
    h = bmp.GetHeight();
    if (w == 0 || h == 0)
        return false;

    BitmapData data;
    Rect rect(0, 0, w, h);
    if (bmp.LockBits(&rect, ImageLockModeRead, PixelFormat32bppARGB, &data) != Ok)
        return false;

    stride = data.Stride;
    size_t rowBytes = static_cast<size_t>(abs(stride));
    size_t dataSize = rowBytes * h;
    pixels.resize(dataSize);

    if (stride > 0) {
        memcpy(pixels.data(), data.Scan0, dataSize);
    } else {
        const BYTE* src = static_cast<const BYTE*>(data.Scan0) + stride * (h - 1);
        BYTE* dst = pixels.data();
        for (int y = 0; y < h; ++y) {
            memcpy(dst, src, rowBytes);
            src -= static_cast<ptrdiff_t>(stride);
            dst += rowBytes;
        }
        stride = static_cast<int>(rowBytes);
    }

    bmp.UnlockBits(&data);
    return true;
}

struct OcrResult {
    std::wstring file;
    bool success = false;
    std::string text;
    std::string error;
};

static std::string EscapeJson(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x",
                             static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

static std::string WStringToUtf8(const std::wstring& wstr)
{
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1,
                                  nullptr, 0, nullptr, nullptr);
    std::string result(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1,
                        result.data(), len, nullptr, nullptr);
    return result;
}

static void PrintUsage()
{
    fprintf(stderr,
        "Usage: OcrCli.exe <image1> [image2 ...] [options]\n"
        "Options:\n"
        "  -m <dir>    Models directory (default: <exe_dir>/models)\n"
        "  -o <file>   Output to UTF-8 file (default: stdout)\n"
        "  -d <path>   Path to OcrDll.dll (default: <exe_dir>/OcrDll.dll)\n"
        "  -j, --json  Output as JSON array (for programmatic use)\n"
        "  -q, --quiet Suppress stderr diagnostics\n"
        "Env:\n"
        "  OCR_CLI_MODELS_DIR  Default models directory\n"
        "  OCR_CLI_DLL_PATH    Default OcrDll.dll path\n");
}

int wmain(int argc, wchar_t* argv[])
{
    if (argc < 2) {
        PrintUsage();
        return 1;
    }

    std::wstring exeDir = GetExeDir();
    std::wstring dllPath = exeDir + L"\\OcrDll.dll";
    std::wstring modelsDir = exeDir + L"\\models";
    std::wstring outputFile;
    std::vector<std::wstring> images;
    bool jsonMode = false;
    bool quietMode = false;

    wchar_t envBuf[MAX_PATH];
    if (GetEnvironmentVariableW(L"OCR_CLI_MODELS_DIR", envBuf, MAX_PATH))
        modelsDir = envBuf;
    if (GetEnvironmentVariableW(L"OCR_CLI_DLL_PATH", envBuf, MAX_PATH))
        dllPath = envBuf;

    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == L'-' || argv[i][0] == L'/') {
            std::wstring opt = argv[i];
            if (opt == L"--json" || opt == L"/json") { jsonMode = true; }
            else if (opt == L"--quiet" || opt == L"/quiet") { quietMode = true; }
            else if (opt.size() == 2 && argv[i][2] == L'\0') {
                wchar_t c = argv[i][1];
                if (c == L'm' && i + 1 < argc) modelsDir = argv[++i];
                else if (c == L'o' && i + 1 < argc) outputFile = argv[++i];
                else if (c == L'd' && i + 1 < argc) dllPath = argv[++i];
                else if (c == L'j') jsonMode = true;
                else if (c == L'q') quietMode = true;
                else {
                    if (!quietMode)
                        fprintf(stderr, "Unknown option: %ws\n", argv[i]);
                    return 1;
                }
            } else {
                if (!quietMode)
                    fprintf(stderr, "Unknown option: %ws\n", argv[i]);
                return 1;
            }
        } else {
            images.push_back(argv[i]);
        }
    }

    if (images.empty()) {
        if (!quietMode)
            fprintf(stderr, "Error: No image files specified\n");
        return 1;
    }

    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    if (GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr) != Ok) {
        if (!quietMode)
            fprintf(stderr, "Error: Failed to initialize GDI+\n");
        return 1;
    }

    OcrDll dll;
    if (!LoadOcrDll(dllPath.c_str(), dll, quietMode)) {
        GdiplusShutdown(gdiplusToken);
        return 1;
    }

    int modelsLen = WideCharToMultiByte(CP_UTF8, 0, modelsDir.c_str(), -1,
                                        nullptr, 0, nullptr, nullptr);
    std::string modelsDirUtf8(modelsLen - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, modelsDir.c_str(), -1,
                        modelsDirUtf8.data(), modelsLen, nullptr, nullptr);

    void* ocrHandle = dll.OcrInit(modelsDirUtf8.c_str());
    if (!ocrHandle) {
        if (!quietMode)
            fprintf(stderr, "Error: OcrInit failed (check models directory: %ws)\n",
                    modelsDir.c_str());
        FreeLibrary(dll.module);
        GdiplusShutdown(gdiplusToken);
        return 1;
    }

    SetConsoleOutputCP(CP_UTF8);

    FILE* outFile = nullptr;
    if (!outputFile.empty()) {
        _wfopen_s(&outFile, outputFile.c_str(), L"wb");
        if (!outFile) {
            if (!quietMode)
                fprintf(stderr, "Error: Cannot open output file: %ws\n",
                        outputFile.c_str());
            dll.OcrDestroy(ocrHandle);
            FreeLibrary(dll.module);
            GdiplusShutdown(gdiplusToken);
            return 1;
        }
    }

    int exitCode = 0;
    std::vector<OcrResult> results;

    for (const auto& imgPath : images) {
        int w, h, stride;
        std::vector<unsigned char> pixels;
        OcrResult r;
        r.file = imgPath;

        if (!ReadImageToBGRA(imgPath.c_str(), pixels, w, h, stride)) {
            r.success = false;
            r.error = "Cannot read image";
            if (!quietMode)
                fprintf(stderr, "Error: Cannot read image: %ws\n", imgPath.c_str());
            exitCode = 1;
            if (jsonMode) results.push_back(r);
            continue;
        }

        char* result = dll.OcrRecognize(ocrHandle, pixels.data(), w, h, stride);
        if (result) {
            r.success = true;
            r.text = result;
            size_t len = strlen(result);
            if (jsonMode) {
                results.push_back(r);
            } else {
                if (outFile) {
                    fwrite(result, 1, len, outFile);
                    fwrite("\n", 1, 1, outFile);
                } else {
                    printf("%s\n", result);
                }
            }
            dll.OcrFreeString(result);
        } else {
            r.success = false;
            r.error = "OCR returned no result";
            if (!quietMode)
                fprintf(stderr, "Warning: OCR returned no result for %ws\n",
                        imgPath.c_str());
            exitCode = 1;
            if (jsonMode) results.push_back(r);
        }
    }

    if (jsonMode) {
        std::string json;
        json += "[\n";
        for (size_t i = 0; i < results.size(); ++i) {
            json += "  {\"file\":\"";
            json += EscapeJson(WStringToUtf8(results[i].file));
            json += "\",\"text\":";
            if (results[i].success) {
                json += "\"";
                json += EscapeJson(results[i].text);
                json += "\",\"success\":true";
            } else {
                json += "null,\"error\":\"";
                json += EscapeJson(results[i].error);
                json += "\",\"success\":false";
            }
            json += "}";
            if (i < results.size() - 1) json += ",";
            json += "\n";
        }
        json += "]\n";
        if (outFile) {
            fwrite(json.data(), 1, json.size(), outFile);
        } else {
            printf("%s", json.c_str());
        }
    }

    if (outFile) fclose(outFile);
    dll.OcrDestroy(ocrHandle);
    FreeLibrary(dll.module);
    GdiplusShutdown(gdiplusToken);
    return exitCode;
}