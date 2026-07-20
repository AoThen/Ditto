#define OCRDLL_EXPORTS
#include "OcrDllInterface.h"
#include <string>
#include <cstring>
#include <vector>
#include <cstdint>
#include <windows.h>

#pragma pack(push, 8)
typedef struct { int padding; int maxSideLen; float boxScoreThresh; float boxThresh; float unClipRatio; int doAngle; int mostAngle; } OCR_PARAM;
typedef struct { uint8_t *data; int type; int channels; int width; int height; long dataLength; } OCR_INPUT;
typedef struct { double x; double y; } OCR_POINT;
typedef struct { OCR_POINT* boxPoint; float boxScore; int angleIndex; float angleScore; double angleTime; uint8_t *text; float *charScores; unsigned long long charScoresLength; unsigned long long boxPointLength; unsigned long long textLength; double crnnTime; double blockTime; } TEXT_BLOCK;
typedef struct { double dbNetTime; TEXT_BLOCK *textBlocks; unsigned long long textBlocksLength; double detectTime; } OCR_RESULT;
#pragma pack(pop)

static HMODULE s_rapidLib = nullptr;
static void* (*s_OcrInit)(const char*, const char*, const char*, const char*, int) = nullptr;
static int (*s_OcrDetectInput)(void*, OCR_INPUT*, OCR_PARAM*, OCR_RESULT*) = nullptr;
static int (*s_OcrFreeResult)(OCR_RESULT*) = nullptr;
static void (*s_OcrDestroy)(void*) = nullptr;

static bool LoadRapidOcr()
{
    if (s_rapidLib) return true;
    s_rapidLib = LoadLibraryA("OcrLiteCApi.dll");
    if (!s_rapidLib) return false;
    s_OcrInit = (void*(*)(const char*, const char*, const char*, const char*, int))GetProcAddress(s_rapidLib, "OcrInit");
    s_OcrDetectInput = (int(*)(void*, OCR_INPUT*, OCR_PARAM*, OCR_RESULT*))GetProcAddress(s_rapidLib, "OcrDetectInput");
    s_OcrFreeResult = (int(*)(OCR_RESULT*))GetProcAddress(s_rapidLib, "OcrFreeResult");
    s_OcrDestroy = (void(*)(void*))GetProcAddress(s_rapidLib, "OcrDestroy");
    return s_OcrInit && s_OcrDetectInput && s_OcrFreeResult && s_OcrDestroy;
}

OCR_API void* OcrInit(const char* modelsDir)
{
    if (!LoadRapidOcr()) return nullptr;
    std::string dir(modelsDir);
    std::string detPath = dir + "/ch_PP-OCRv3_det_infer.onnx";
    std::string clsPath = dir + "/ch_ppocr_mobile_v2.0_cls_infer.onnx";
    std::string recPath = dir + "/ch_PP-OCRv3_rec_infer.onnx";
    std::string keysPath = dir + "/ppocr_keys_v1.txt";
    return s_OcrInit(detPath.c_str(), clsPath.c_str(), recPath.c_str(), keysPath.c_str(), 4);
}

OCR_API char* OcrRecognize(void* handle, const unsigned char* imageData,
                            int width, int height, int stride)
{
    if (!LoadRapidOcr()) return nullptr;
    std::vector<uint8_t> bgr(width * 3 * height);
    for (int y = 0; y < height; y++) {
        const uint8_t* src = imageData + y * stride;
        uint8_t* dst = bgr.data() + y * width * 3;
        for (int x = 0; x < width; x++) {
            dst[x * 3] = src[x * 4];
            dst[x * 3 + 1] = src[x * 4 + 1];
            dst[x * 3 + 2] = src[x * 4 + 2];
        }
    }
    OCR_INPUT input = { bgr.data(), 0, 3, width, height, (long)bgr.size() };
    OCR_PARAM param = { 50, 1024, 0.6f, 0.3f, 2.0f, 1, 1 };
    OCR_RESULT ocrResult = {};
    if (!s_OcrDetectInput(handle, &input, &param, &ocrResult)) {
        return nullptr;
    }
    std::string result;
    for (unsigned long long i = 0; i < ocrResult.textBlocksLength; i++) {
        if (ocrResult.textBlocks[i].textLength > 0) {
            result.append((const char*)ocrResult.textBlocks[i].text,
                          ocrResult.textBlocks[i].textLength);
        }
    }
    s_OcrFreeResult(&ocrResult);
    if (result.empty()) return nullptr;
    char* ret = (char*)malloc(result.length() + 1);
    if (ret) memcpy(ret, result.c_str(), result.length() + 1);
    return ret;
}

OCR_API void OcrFreeString(char* str)
{
    if (str) free(str);
}

OCR_API void OcrDestroy(void* handle)
{
    if (LoadRapidOcr()) s_OcrDestroy(handle);
}