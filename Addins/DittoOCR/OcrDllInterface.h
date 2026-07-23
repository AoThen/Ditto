#pragma once

#ifdef OCRDLL_EXPORTS
#define OCR_API __declspec(dllexport)
#else
#define OCR_API __declspec(dllimport)
#endif

extern "C"
{
    OCR_API void*  OcrInit(const char* modelsDir);
    OCR_API char*  OcrRecognize(void* handle, const unsigned char* imageData,
                                 int width, int height, int stride);
    OCR_API void   OcrFreeString(char* str);
    OCR_API void   OcrDestroy(void* handle);
}