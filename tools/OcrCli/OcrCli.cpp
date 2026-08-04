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

static bool LoadOcrDll(const wchar_t* dllPath, OcrDll& dll)
{
    dll.module = LoadLibraryW(dllPath);
    if (!dll.module) {
        fprintf(stderr, "Error: Failed to load %ws\n", dllPath);
        return false;
    }
    dll.OcrInit = (void* (*)(const char*))GetProcAddress(dll.module, "OcrInit");
    dll.OcrRecognize = (char* (*)(void*, const unsigned char*, int, int, int))
        GetProcAddress(dll.module, "OcrRecognize");
    dll.OcrFreeString = (void (*)(char*))GetProcAddress(dll.module, "OcrFreeString");
    dll.OcrDestroy = (void (*)(void*))GetProcAddress(dll.module, "OcrDestroy");
    if (!dll.OcrInit || !dll.OcrRecognize || !dll.OcrFreeString || !dll.OcrDestroy) {
        fprintf(stderr, "Error: Failed to locate OCR functions in %ws\n", dllPath);
        FreeLibrary(dll.module);
        dll.module = nullptr;
        return false;
    }
    return true;
}

static std::wstring GetExeDir()
{
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* last = wcsrchr(path, L'\\');
    if (last) *last = L'\0';
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

static void PrintUsage()
{
    fprintf(stderr,
        "Usage: OcrCli.exe <image1> [image2 ...] [options]\n"
        "Options:\n"
        "  -m <dir>    Models directory (default: <exe_dir>/models)\n"
        "  -o <file>   Output to UTF-8 file (default: stdout)\n"
        "  -d <path>   Path to OcrDll.dll (default: <exe_dir>/OcrDll.dll)\n");
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

    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == L'-' || argv[i][0] == L'/') {
            wchar_t opt = argv[i][1];
            if (opt == L'm' && i + 1 < argc) modelsDir = argv[++i];
            else if (opt == L'o' && i + 1 < argc) outputFile = argv[++i];
            else if (opt == L'd' && i + 1 < argc) dllPath = argv[++i];
            else {
                fprintf(stderr, "Unknown option: %ws\n", argv[i]);
                return 1;
            }
        } else {
            images.push_back(argv[i]);
        }
    }

    if (images.empty()) {
        fprintf(stderr, "Error: No image files specified\n");
        return 1;
    }

    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    if (GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr) != Ok) {
        fprintf(stderr, "Error: Failed to initialize GDI+\n");
        return 1;
    }

    OcrDll dll;
    if (!LoadOcrDll(dllPath.c_str(), dll)) {
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
            fprintf(stderr, "Error: Cannot open output file: %ws\n",
                    outputFile.c_str());
            dll.OcrDestroy(ocrHandle);
            FreeLibrary(dll.module);
            GdiplusShutdown(gdiplusToken);
            return 1;
        }
    }

    int exitCode = 0;
    for (const auto& imgPath : images) {
        int w, h, stride;
        std::vector<unsigned char> pixels;
        if (!ReadImageToBGRA(imgPath.c_str(), pixels, w, h, stride)) {
            fprintf(stderr, "Error: Cannot read image: %ws\n", imgPath.c_str());
            exitCode = 1;
            continue;
        }

        char* result = dll.OcrRecognize(ocrHandle, pixels.data(), w, h, stride);
        if (result) {
            size_t len = strlen(result);
            if (outFile) {
                fwrite(result, 1, len, outFile);
                fwrite("\n", 1, 1, outFile);
            } else {
                printf("%s\n", result);
            }
            dll.OcrFreeString(result);
        } else {
            fprintf(stderr, "Warning: OCR returned no result for %ws\n", imgPath.c_str());
            exitCode = 1;
        }
    }

    if (outFile) fclose(outFile);
    dll.OcrDestroy(ocrHandle);
    FreeLibrary(dll.module);
    GdiplusShutdown(gdiplusToken);
    return exitCode;
}