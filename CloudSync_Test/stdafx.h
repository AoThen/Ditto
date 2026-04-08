#pragma once

// stdafx.h : minimal precompiled header for CloudSync tests
// Includes only what CloudCrypto and CloudKeyExport tests need

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tchar.h>

// Minimal ATL for CString (needed by CloudCrypto interface)
#include <atlstr.h>

// MFC file/time classes (needed by CloudKeyExport tests)
#include <afx.h>      // CFile, CTime
#include <afxwin.h>   // CString, basic MFC

// C++ standard headers
#include <vector>
#include <string>
#include <cstring>
#include <cstdio>

