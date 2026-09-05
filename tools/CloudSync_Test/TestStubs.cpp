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
CClipFormat& CClipFormat::operator=(const CClipFormat&) { return *this; }

// -------------------------------------------------------------------
// CGetSetOptions stubs (for methods referenced by compiled source files)
// -------------------------------------------------------------------
int CGetSetOptions::GetPreferUtf8ForCompare() { return TRUE; }
CString CGetSetOptions::GetDiffApp() { return CString(); }
CString CGetSetOptions::ResolvePath(CString path) { return path; }
CString CGetSetOptions::GetPath(long /*lPathID*/) { return CString(); }
CString CGetSetOptions::GetCopyAppSeparator() { return CString(); }
BOOL CGetSetOptions::GetCloudEncryptionNeedsRecovery() { return FALSE; }

// -------------------------------------------------------------------
// log / StrF stubs
// -------------------------------------------------------------------
void log(const TCHAR* /*msg*/, bool /*bFromSendRecieve*/, CString /*csFile*/, long /*lLine*/) {}
void logclip(const TCHAR* /*msg*/, bool /*bFromSendRecieve*/, CString /*csFile*/, long /*lLine*/) {}
CString StrF(const TCHAR* /*pszFormat*/, ...) { return CString(); }

// -------------------------------------------------------------------
// CGetSetOptions cloud-credential stubs
//
// CloudAuth.cpp reads and writes these. They must stay out of
// GetSetOptionsMock.h: an inline definition there is only emitted when some
// test happens to call it, which would leave CloudAuth.obj unresolved (and
// would collide with these strong definitions once it did).
// -------------------------------------------------------------------
static CString s_stubServerUrl;
static CStringA s_stubRefreshToken;
static CString s_stubInstallId;

CString CGetSetOptions::GetCloudServerUrl() { return s_stubServerUrl; }
void CGetSetOptions::SetCloudServerUrl(LPCTSTR lpszValue) { s_stubServerUrl = lpszValue; }
CStringA CGetSetOptions::GetCloudRefreshToken() { return s_stubRefreshToken; }
void CGetSetOptions::SetCloudRefreshToken(LPCSTR lpszValue) { s_stubRefreshToken = lpszValue; }
CString CGetSetOptions::GetCloudInstallId() { return s_stubInstallId; }
void CGetSetOptions::SetCloudInstallId(LPCTSTR lpszValue) { s_stubInstallId = lpszValue; }

// -------------------------------------------------------------------
// Misc.cpp stubs
// -------------------------------------------------------------------
// Fixed id keeps the login payload deterministic in tests.
CString NewGuidString() { return _T("11111111-2222-3333-4444-555555555555"); }