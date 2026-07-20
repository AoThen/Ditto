#pragma once
#include "Clip.h"

bool ExtractClipImageData(CClip* pClip);
CStringW RunOCR(const std::vector<BYTE>& data, int w, int h, int stride);
void CleanupOCR();