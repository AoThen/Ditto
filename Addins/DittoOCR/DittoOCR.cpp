#define OCRDLL_EXPORTS
#include "OcrDllInterface.h"
#include <string>
#include <cstring>

// RapidOcrOnnx includes
#include "rapidocr_onnx/include/OcrLiteCApi.h"

// OpenCV
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

OCR_API void* OcrInit(const char* modelsDir)
{
    return OcrInitEx(modelsDir);
}

OCR_API char* OcrRecognize(void* handle, const unsigned char* imageData,
                            int width, int height, int stride)
{
    // Create OpenCV Mat from BGRA data with stride
    cv::Mat bgra(height, width, CV_8UC4, (void*)imageData, stride);
    cv::Mat bgr;
    cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);

    // Call RapidOcrOnnx
    const char* text = OcrDetectInput(handle, bgr.data, bgr.cols, bgr.rows);
    if (!text)
        return nullptr;

    size_t len = strlen(text);
    char* result = (char*)malloc(len + 1);
    if (result)
        memcpy(result, text, len + 1);
    return result;
}

OCR_API void OcrFreeString(char* str)
{
    if (str)
        free(str);
}

OCR_API void OcrDestroy(void* handle)
{
    OcrDestroyEx(handle);
}