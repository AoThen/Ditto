#include "stdafx.h"
#include "ClipboardOCR.h"
#include "Options.h"
#include "CP_Main.h"
#include <memory>

static CCriticalSection g_cs;
static HMODULE          g_hDll = nullptr;
static void*            g_ocrHandle = nullptr;
// 0=unloaded, 1=ready, 2=failed
static int              g_ocrState = 0;

// DLL function pointer types
typedef void* (*PFN_OcrInit)(const char* modelsDir);
typedef char* (*PFN_OcrRecognize)(void* handle, const unsigned char* imageData, int width, int height, int stride);
typedef void  (*PFN_OcrFreeString)(char* str);
typedef void  (*PFN_OcrDestroy)(void* handle);

static PFN_OcrInit        g_pfnOcrInit = nullptr;
static PFN_OcrRecognize   g_pfnOcrRecognize = nullptr;
static PFN_OcrFreeString  g_pfnOcrFreeString = nullptr;
static PFN_OcrDestroy     g_pfnOcrDestroy = nullptr;

std::atomic<int> g_ocrThreadCount{0};

bool ExtractClipImageData(CClip* pClip)
{
    auto* fmt = pClip->m_Formats.FindFormat(CF_DIB);
    if (!fmt)
        fmt = pClip->m_Formats.FindFormat(theApp.m_PNG_Format);
    if (!fmt)
    {
        Log(StrF(_T("OCR: ExtractClipImageData - no CF_DIB or PNG format in clip")));
        return false;
    }

    Gdiplus::Bitmap* bmp = fmt->CreateGdiplusBitmap();
    if (!bmp)
    {
        Log(StrF(_T("OCR: ExtractClipImageData - CreateGdiplusBitmap failed")));
        return false;
    }

    Gdiplus::Rect r(0, 0, bmp->GetWidth(), bmp->GetHeight());
    Gdiplus::BitmapData bd;
    if (bmp->LockBits(&r, Gdiplus::ImageLockModeRead,
            PixelFormat32bppARGB, &bd) != Gdiplus::Ok)
    {
        Log(StrF(_T("OCR: ExtractClipImageData - LockBits failed")));
        delete bmp;
        return false;
    }

    constexpr int kMaxOcrDim = 4096;
    if (bd.Width > kMaxOcrDim || bd.Height > kMaxOcrDim)
    {
        Log(StrF(_T("OCR: ExtractClipImageData - image too large %dx%d, max allowed %d"), bd.Width, bd.Height, kMaxOcrDim));
        bmp->UnlockBits(&bd);
        delete bmp;
        return false;
    }
    ULONGLONG sizeNeeded = (ULONGLONG)bd.Height * (ULONGLONG)bd.Stride;
    if (sizeNeeded > 256ULL * 1024 * 1024)
    {
        Log(StrF(_T("OCR: ExtractClipImageData - image data too large %llu bytes"), sizeNeeded));
        bmp->UnlockBits(&bd);
        delete bmp;
        return false;
    }
    pClip->m_ocrImageData.resize((size_t)sizeNeeded);
    memcpy(pClip->m_ocrImageData.data(), bd.Scan0, (size_t)sizeNeeded);
    pClip->m_ocrWidth  = bd.Width;
    pClip->m_ocrHeight = bd.Height;
    pClip->m_ocrStride = bd.Stride;

    bmp->UnlockBits(&bd);
    delete bmp;
    return true;
}

// Called from worker thread (detached). Must be thread-safe.
CStringW RunOCR(const std::vector<BYTE>& data, int w, int h, int stride)
{
    if (data.empty() || w <= 0 || h <= 0)
    {
        Log(StrF(_T("OCR: RunOCR called with empty data or invalid dimensions")));
        return L"";
    }

    CSingleLock lock(&g_cs, TRUE);

    if (g_ocrState == 2)
    {
        Log(StrF(_T("OCR: RunOCR - state=2 (previously failed), skip")));
        return L"";
    }

    // First-time initialization
    if (!g_hDll)
    {
        CString path = CGetSetOptions::GetPath(PATH_ADDINS) + _T("DittoOCR\\OcrDll.dll");
        g_hDll = ::LoadLibrary(path);
        if (!g_hDll)
        {
            Log(StrF(_T("OCR: LoadLibrary OcrDll.dll failed, path=%s"), path));
            g_ocrState = 2;
            return L"";
        }
        g_pfnOcrInit        = (PFN_OcrInit)::GetProcAddress(g_hDll, "OcrInit");
        g_pfnOcrRecognize   = (PFN_OcrRecognize)::GetProcAddress(g_hDll, "OcrRecognize");
        g_pfnOcrFreeString  = (PFN_OcrFreeString)::GetProcAddress(g_hDll, "OcrFreeString");
        g_pfnOcrDestroy     = (PFN_OcrDestroy)::GetProcAddress(g_hDll, "OcrDestroy");
        if (!g_pfnOcrInit || !g_pfnOcrRecognize || !g_pfnOcrFreeString || !g_pfnOcrDestroy)
        {
            Log(StrF(_T("OCR: OcrDll.dll missing required exports")));
            ::FreeLibrary(g_hDll);
            g_hDll = nullptr;
            g_ocrState = 2;
            return L"";
        }
    }

    if (!g_ocrHandle)
    {
        CString modelsDir = CGetSetOptions::GetPath(PATH_ADDINS) + _T("DittoOCR\\models\\");
        CStringA modelsDirA = CW2A(modelsDir, CP_UTF8);
        g_ocrHandle = g_pfnOcrInit(modelsDirA);
        if (!g_ocrHandle)
        {
            Log(StrF(_T("OCR: OcrInit returned null, modelsDir=%s"), modelsDir));
            g_ocrState = 2;
            return L"";
        }
        g_ocrState = 1;
    }

    if (g_ocrState != 1)
        return L"";

    char* text = g_pfnOcrRecognize(g_ocrHandle, data.data(), w, h, stride);
    if (!text)
    {
        Log(StrF(_T("OCR: OcrRecognize returned null")));
        return L"";
    }

    CStringW result = CA2W(text, CP_UTF8);
    g_pfnOcrFreeString(text);
    Log(StrF(_T("OCR: RunOCR success, text=%s"), result));
    return result;
}

void CleanupOCR()
{
    CSingleLock lock(&g_cs, TRUE);
    if (g_ocrHandle && g_pfnOcrDestroy)
    {
        g_pfnOcrDestroy(g_ocrHandle);
        g_ocrHandle = nullptr;
    }
    if (g_hDll)
    {
        ::FreeLibrary(g_hDll);
        g_hDll = nullptr;
    }
    g_ocrState = 0;
    g_pfnOcrInit = nullptr;
    g_pfnOcrRecognize = nullptr;
    g_pfnOcrFreeString = nullptr;
    g_pfnOcrDestroy = nullptr;
}