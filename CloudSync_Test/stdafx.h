#pragma once

// stdafx.h : minimal precompiled header for CloudSync tests
// Includes only what CloudCrypto and CloudKeyExport tests need

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tchar.h>

// MFC headers (provides CString, CFile, CTime, etc.)
// Note: Do NOT include <atlstr.h> before MFC headers - MFC provides its own CString
#include <afx.h>      // CFile, CTime, CString
#include <afxwin.h>   // Basic MFC and CString

// C++ standard headers
#include <vector>
#include <string>
#include <cstring>
#include <cstdio>

