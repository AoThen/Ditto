// TestStubs.cpp - Stub implementations for symbols needed by CloudSync_Test
// These allow the test project to link without pulling in the full Ditto source.

#include "stdafx.h"
#include "Clip.h"
#include "Options.h"
#include "Misc.h"

// -------------------------------------------------------------------
// CClip stubs
// -------------------------------------------------------------------
CClip::CClip() {}
CClip::~CClip() {}
bool CClip::LoadFormats(int /*id*/, bool /*bOnlyLoad_CF_TEXT*/, bool /*includeRichTextForTextOnly*/, int /*dataId*/) { return false; }
CStringW CClip::GetUnicodeTextFormat() { return CStringW(); }
CStringA CClip::GetCFTextTextFormat() { return CStringA(); }
BOOL CClip::WriteTextToFile(CString /*path*/, BOOL /*unicode*/, BOOL /*asci*/, BOOL /*rtf*/, BOOL /*forceUnicode*/, BOOL /*utf8*/) { return FALSE; }

// -------------------------------------------------------------------
// CClipFormat / CClipFormats stubs (needed by CClip::~CClip() via CArray<CClipFormat> member)
// -------------------------------------------------------------------
CClipFormat::CClipFormat(CLIPFORMAT cfType, HGLOBAL hgData, int parentId) {}
CClipFormat::~CClipFormat() {}
CClipFormat* CClipFormats::FindFormat(UINT cfType) { return nullptr; }
bool CClipFormats::RemoveFormat(CLIPFORMAT cfType) { return false; }
void CClipFormat::Free() {}
Gdiplus::Bitmap* CClipFormat::CreateGdiplusBitmap() { return nullptr; }

// -------------------------------------------------------------------
// CGetSetOptions stubs (for methods referenced by compiled source files)
// -------------------------------------------------------------------
int CGetSetOptions::GetPreferUtf8ForCompare() { return TRUE; }
CString CGetSetOptions::GetDiffApp() { return CString(); }
CString CGetSetOptions::ResolvePath(CString path) { return path; }
CString CGetSetOptions::GetPath(long /*lPathID*/) { return CString(); }
CString CGetSetOptions::GetCopyAppSeparator() { return CString(); }

// -------------------------------------------------------------------
// log / StrF stubs
// -------------------------------------------------------------------
void log(const TCHAR* /*msg*/, bool /*bFromSendRecieve*/, CString /*csFile*/, long /*lLine*/) {}
CString StrF(const TCHAR* /*pszFormat*/, ...) { return CString(); }