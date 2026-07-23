#pragma once
#include "..\..\src\shared\DittoDefines.h"
#include "..\..\src\shared\IClip.h"


class PasteAnyAsText
{
public:
	PasteAnyAsText(void);
	~PasteAnyAsText(void);

	static bool SelectClipToPasteAsText(const CDittoInfo &DittoInfo, IClip *pClip);
};
