#pragma once
#include "Clip.h"
#include <atomic>

bool ExtractClipImageData(CClip* pClip);
CStringW RunOCR(const std::vector<BYTE>& data, int w, int h, int stride);
void CleanupOCR();

extern std::atomic<int> g_ocrThreadCount;