#include "stdafx.h"
#include "CP_Main.h"
#include "Misc.h"
#include "OptionsSheet.h"
#include "shared\TextConvert.h"
#include "AlphaBlend.h"
#include "Tlhelp32.h"
#include <Wininet.h>
#include <sys/types.h>  
#include <sys/stat.h> 
#include "Path.h"
#include <regex>
#include <vector>

CString GetIPAddress()
{
	WORD wVersionRequested;
    WSADATA wsaData;
    char name[255];
    CString IP;
	PHOSTENT hostinfo;
	wVersionRequested = MAKEWORD(2,0);
	
	if (WSAStartup(wVersionRequested, &wsaData)==0)
	{
		if(gethostname(name, sizeof(name))==0)
		{
			if((hostinfo=gethostbyname(name)) != NULL)
			{
				IP = inet_ntoa(*(struct in_addr*)* hostinfo->h_addr_list);
			}
		}
		
		WSACleanup();
	} 
	IP.MakeUpper();

	return IP;
}

CString GetComputerName()
{
	TCHAR ComputerName[MAX_COMPUTERNAME_LENGTH+1] = _T("");
	DWORD Size=MAX_COMPUTERNAME_LENGTH+1;
	GetComputerName(ComputerName, &Size);

	CString cs(ComputerName);
	cs.MakeUpper();

	return cs;
}

static CCriticalSection g_csLog;

void AppendToFile(const TCHAR* fn, const TCHAR* msg)
{
	CSingleLock lock(&g_csLog, TRUE);

#ifdef _UNICODE
	FILE *file = _wfopen(fn, L"ab");
#else
	FILE *file = fopen(fn, _T("ab"));
#endif

	ASSERT( file );

	if(file != NULL)
	{
		#ifdef _UNICODE
			CStringA utf8Msg = CW2A(msg, CP_UTF8);
			fwrite((LPCSTR)utf8Msg, 1, utf8Msg.GetLength(), file);
		#else
			fprintf(file, _T("%s"), msg);
		#endif

		fclose(file);	
	}
}

void log(const TCHAR* msg, bool bFromSendRecieve, CString csFile, long lLine)
{
	ASSERT(AfxIsValidString(msg));

	SYSTEMTIME st;
	GetLocalTime(&st);
	
	CString	csText;
	csText.Format(_T("[%d/%d/%d %02d:%02d:%02d.%03d - "), st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

	CString csFileLine;
	csFile = GetFileName(csFile);
	csFileLine.Format(_T("%s %d] "), csFile, lLine);
	csText += csFileLine;
	
	csText += msg;
	csText += "\n";

#ifndef _DEBUG
	if(CGetSetOptions::m_outputDebugStringLogging)
#endif
	{
		OutputDebugString(csText);
	}

#ifndef _DEBUG
	if(!bFromSendRecieve)
	{
		if(!CGetSetOptions::m_bEnableDebugLogging)
			return;
	}
#endif
	
	CString csExeFile = CGetSetOptions::GetPath(PATH_LOG_FILE);
	csExeFile += "Ditto.log";

	AppendToFile(csExeFile, csText);
}

void logclip(const TCHAR* msg, bool bFromSendRecieve, CString csFile, long lLine)
{
	if(!CGetSetOptions::m_bLogClipboardContent)
		return;
	log(msg, bFromSendRecieve, csFile, lLine);
}

void logsendrecieveinfo(CString cs, CString csFile, long lLine)
{
	if(CGetSetOptions::m_bLogSendReceiveErrors)
		log(cs, true, csFile, lLine);
}

int g_funnyGetTickCountAdjustment = -1;

double IdleSeconds()
{
	LASTINPUTINFO info; 
	info.cbSize = sizeof(info);
	GetLastInputInfo(&info);   
	DWORD currentTick  = GetTickCount();

	if(g_funnyGetTickCountAdjustment == -1)
	{
		if(currentTick < info.dwTime)
		{
			g_funnyGetTickCountAdjustment = 1;
		}
		else
		{
			g_funnyGetTickCountAdjustment = 0; 
		}		
	}
	
	if(g_funnyGetTickCountAdjustment == 1 || g_funnyGetTickCountAdjustment == 2)
	{
		//Output message the first time
		if(g_funnyGetTickCountAdjustment == 1)
		{
			Log(StrF(_T("Adjusting time of get tickcount by: %d, on startup we found GetTickCount to be less than last input"), CGetSetOptions::GetFunnyTickCountAdjustment()));
			g_funnyGetTickCountAdjustment = 2;
		}
		currentTick += CGetSetOptions::GetFunnyTickCountAdjustment();
	}

	double idleSeconds = (currentTick - info.dwTime)/1000.0;

	return idleSeconds;
}

CString StrF(const TCHAR * pszFormat, ...)
{
	ASSERT( AfxIsValidString( pszFormat ) );
	CString str;
	va_list argList;
	va_start( argList, pszFormat );
	str.FormatV( pszFormat, argList );
	va_end( argList );
	return str;
}

CString GetWndText(HWND hWnd)
{
	TCHAR cWindowText[200];
	HWND hParent = hWnd;

	::GetWindowText(hParent, cWindowText, 100);

	int nCount = 0;

	while (STRLEN(cWindowText) <= 0)
	{
		hParent = ::GetParent(hParent);
		if (hParent == NULL)
			break;

		::GetWindowText(hParent, cWindowText, 100);

		nCount++;
		if (nCount > 100)
		{
			Log(_T("GetTargetName reached maximum search depth of 100"));
			break;
		}
	}

	return cWindowText;
}

CString TopLevelWindowText(DWORD pid)
{
	std::pair<CString, DWORD> params = { _T(""), pid };

	// Enumerate the windows using a lambda to process each window
	BOOL bResult = EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL
	{
		auto pParams = (std::pair<CString, DWORD>*)(lParam);

		DWORD processId;
		if (GetWindowThreadProcessId(hwnd, &processId) &&
			processId == pParams->second &&
			::GetWindow(hwnd, GW_OWNER) == 0)
		{
			TCHAR cWindowText[500];
			::GetWindowText(hwnd, cWindowText, 500);

			if (STRLEN(cWindowText) > 0)
			{
				pParams->first = cWindowText;
				return FALSE;
			}
		}

		// Continue enumerating
		return TRUE;
	}, (LPARAM)&params);

	return params.first;
}

bool IsAppWnd( HWND hWnd )
{
	DWORD dwMyPID = ::GetCurrentProcessId();
	DWORD dwTestPID;
	::GetWindowThreadProcessId( hWnd, &dwTestPID );
	return dwMyPID == dwTestPID;
}

/*----------------------------------------------------------------------------*\
Global Memory Helper Functions
\*----------------------------------------------------------------------------*/

// asserts if hDest isn't big enough
BOOL IsValid(HGLOBAL hGlobal)
{
	void* pvData = ::GlobalLock(hGlobal);
	::GlobalUnlock(hGlobal);
	return (pvData != NULL);
}

void CopyToGlobalHP(HGLOBAL hDest, LPVOID pBuf, SIZE_T ulBufLen)
{
	if (!hDest || !pBuf || ulBufLen == 0) return;
	LPVOID pvData = GlobalLock(hDest);
	if (!pvData) return;
	SIZE_T size = GlobalSize(hDest);
	if (size < ulBufLen) { GlobalUnlock(hDest); return; }
	memcpy(pvData, pBuf, ulBufLen);
	GlobalUnlock(hDest);
}

void CopyToGlobalHH(HGLOBAL hDest, HGLOBAL hSource, SIZE_T ulBufLen)
{
	if (!hDest || !hSource || ulBufLen == 0) return;
	LPVOID pvData = GlobalLock(hSource);
	if (!pvData) return;
	SIZE_T size = GlobalSize(hSource);
	if (size < ulBufLen) { GlobalUnlock(hSource); return; }
	CopyToGlobalHP(hDest, pvData, ulBufLen);
	GlobalUnlock(hSource);
}

HGLOBAL NewGlobalP(LPVOID pBuf, SIZE_T nLen)
{
	if (!pBuf || nLen == 0) return nullptr;
	HGLOBAL hDest = GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, nLen);
	if (!hDest) return nullptr;
	CopyToGlobalHP(hDest, pBuf, nLen);
	return hDest;
}

HGLOBAL NewGlobal(SIZE_T nLen)
{
	if (nLen == 0) return nullptr;
	HGLOBAL hDest = GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, nLen);
	return hDest;
}

HGLOBAL NewGlobalH(HGLOBAL hSource, SIZE_T nLen)
{
	if (!hSource || nLen == 0) return nullptr;
	LPVOID pvData = GlobalLock(hSource);
	if (!pvData) return nullptr;
	HGLOBAL hDest = NewGlobalP(pvData, nLen);
	GlobalUnlock(hSource);
	return hDest;
}

// https://learn.microsoft.com/en-us/windows/win32/dataxchg/standard-clipboard-formats
std::vector<CLIPFORMAT> GetSystemClipFormats()
{
	std::vector<CLIPFORMAT> v = {
		CF_TEXT,
		CF_BITMAP,
		CF_METAFILEPICT,
		CF_SYLK,
		CF_DIF,
		CF_TIFF,
		CF_OEMTEXT,
		CF_DIB,
		CF_PALETTE,
		CF_PENDATA,
		CF_RIFF,
		CF_WAVE,
		CF_UNICODETEXT,
		CF_ENHMETAFILE,
		CF_HDROP,
		CF_LOCALE,
		CF_OWNERDISPLAY,
		CF_DSPTEXT,
		CF_DSPBITMAP,
		CF_DSPMETAFILEPICT,
		CF_DSPENHMETAFILE
	};

	return v;
}

//Do not change these these are stored in the database
CLIPFORMAT GetFormatID(LPCTSTR cbName)
{
	if (STRCMP(cbName, _T("CF_TEXT")) == 0)
		return CF_TEXT;
	else if (STRCMP(cbName, _T("CF_METAFILEPICT")) == 0)
		return CF_METAFILEPICT;
	else if (STRCMP(cbName, _T("CF_SYLK")) == 0)
		return CF_SYLK;
	else if (STRCMP(cbName, _T("CF_DIF")) == 0)
		return CF_DIF;
	else if (STRCMP(cbName, _T("CF_TIFF")) == 0)
		return CF_TIFF;
	else if (STRCMP(cbName, _T("CF_OEMTEXT")) == 0)
		return CF_OEMTEXT;
	else if (STRCMP(cbName, _T("CF_DIB")) == 0)
		return CF_DIB;
	else if (STRCMP(cbName, _T("CF_PALETTE")) == 0)
		return CF_PALETTE;
	else if (STRCMP(cbName, _T("CF_PENDATA")) == 0)
		return CF_PENDATA;
	else if (STRCMP(cbName, _T("CF_RIFF")) == 0)
		return CF_RIFF;
	else if (STRCMP(cbName, _T("CF_WAVE")) == 0)
		return CF_WAVE;
	else if (STRCMP(cbName, _T("CF_UNICODETEXT")) == 0)
		return CF_UNICODETEXT;
	else if (STRCMP(cbName, _T("CF_ENHMETAFILE")) == 0)
		return CF_ENHMETAFILE;
	else if (STRCMP(cbName, _T("CF_HDROP")) == 0)
		return CF_HDROP;
	else if (STRCMP(cbName, _T("CF_LOCALE")) == 0)
		return CF_LOCALE;
	else if (STRCMP(cbName, _T("CF_OWNERDISPLAY")) == 0)
		return CF_OWNERDISPLAY;
	else if (STRCMP(cbName, _T("CF_DSPTEXT")) == 0)
		return CF_DSPTEXT;
	else if (STRCMP(cbName, _T("CF_DSPBITMAP")) == 0)
		return CF_DSPBITMAP;
	else if (STRCMP(cbName, _T("CF_DSPMETAFILEPICT")) == 0)
		return CF_DSPMETAFILEPICT;
	else if (STRCMP(cbName, _T("CF_DSPENHMETAFILE")) == 0)
		return CF_DSPENHMETAFILE;
	else if (STRCMP(cbName, _T("CF_DIBV5")) == 0)
		return CF_DIBV5;
	
	
	return ::RegisterClipboardFormat(cbName);
}

//Do not change these these are stored in the database
CString GetFormatName(CLIPFORMAT cbType)
{
	switch(cbType)
	{
	case CF_TEXT:
		return _T("CF_TEXT");
	case CF_BITMAP:
		return _T("CF_BITMAP");
	case CF_METAFILEPICT:
		return _T("CF_METAFILEPICT");
	case CF_SYLK:
		return _T("CF_SYLK");
	case CF_DIF:
		return _T("CF_DIF");
	case CF_TIFF:
		return _T("CF_TIFF");
	case CF_OEMTEXT:
		return _T("CF_OEMTEXT");
	case CF_DIB:
		return _T("CF_DIB");
	case CF_PALETTE:
		return _T("CF_PALETTE");
	case CF_PENDATA:
		return _T("CF_PENDATA");
	case CF_RIFF:
		return _T("CF_RIFF");
	case CF_WAVE:
		return _T("CF_WAVE");
	case CF_UNICODETEXT:
		return _T("CF_UNICODETEXT");
	case CF_ENHMETAFILE:
		return _T("CF_ENHMETAFILE");
	case CF_HDROP:
		return _T("CF_HDROP");
	case CF_LOCALE:
		return _T("CF_LOCALE");
	case CF_OWNERDISPLAY:
		return _T("CF_OWNERDISPLAY");
	case CF_DSPTEXT:
		return _T("CF_DSPTEXT");
	case CF_DSPBITMAP:
		return _T("CF_DSPBITMAP");
	case CF_DSPMETAFILEPICT:
		return _T("CF_DSPMETAFILEPICT");
	case CF_DSPENHMETAFILE:
		return _T("CF_DSPENHMETAFILE");
	case CF_DIBV5:
		return _T("CF_DIBV5");
	default:
		//Not a default type get the name from the clipboard
		if (cbType != 0)
		{
			TCHAR szFormat[256];
            GetClipboardFormatName(cbType, szFormat, 256);
			return szFormat;
		}
		break;
	}
	
	return "ERROR";
}

CString GetFilePath(CString csFileName)
{
	long lSlash = csFileName.ReverseFind('\\');
	
	if(lSlash > -1)
	{
		csFileName = csFileName.Left(lSlash + 1);
	}
	
	return csFileName;
}

CString GetFileName(CString csFileName)
{
	long lSlash = csFileName.ReverseFind('\\');
	if(lSlash > -1)
	{
		csFileName = csFileName.Right(csFileName.GetLength() - lSlash - 1);
	}

	return csFileName;
}


/****************************************************************************************************
BOOL CALLBACK MyMonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData)
***************************************************************************************************/
typedef struct
{
	long	lFlags;				// Flags
	LPRECT	pVirtualRect;		// Ptr to rect that receives the results, or the src of the monitor search method
	int		iMonitor;			// Ndx to the mointor to look at, -1 for all, -or- result of the monitor search method
	int		nMonitorCount;		// Total number of monitors found, -1 for monitor search method
}	MONITOR_ENUM_PARAM;
#define	MONITOR_SEARCH_METOHD	0x00000001
BOOL CALLBACK MyMonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData)
{
	// Typecast param
	MONITOR_ENUM_PARAM* pParam = (MONITOR_ENUM_PARAM*)dwData;
	if(pParam)
	{
		// If a dest rect was passed
		if(pParam->pVirtualRect)
		{
			// If MONITOR_SEARCH_METOHD then we are being asked for the index of the monitor
			// that the rect falls inside of
			if(pParam->lFlags & MONITOR_SEARCH_METOHD)
			{
				if(	(pParam->pVirtualRect->right	< lprcMonitor->left)	||
					(pParam->pVirtualRect->left		> lprcMonitor->right)	||
					(pParam->pVirtualRect->bottom	< lprcMonitor->top)		||
					(pParam->pVirtualRect->top		> lprcMonitor->bottom))
				{
					// Nothing
				}
				else
				{
					// This is the one
					pParam->iMonitor = pParam->nMonitorCount;
					
					// Stop the enumeration
					return FALSE;
				}
			}
			else
			{
				if(pParam->iMonitor == pParam->nMonitorCount)
				{
					*pParam->pVirtualRect = *lprcMonitor;
				}
				else
					if(pParam->iMonitor == -1)
					{
						pParam->pVirtualRect->left = min(pParam->pVirtualRect->left, lprcMonitor->left);
						pParam->pVirtualRect->top = min(pParam->pVirtualRect->top, lprcMonitor->top);
						pParam->pVirtualRect->right = max(pParam->pVirtualRect->right, lprcMonitor->right);
						pParam->pVirtualRect->bottom = max(pParam->pVirtualRect->bottom, lprcMonitor->bottom);
					}
			}
		}
		
		// Up the count if necessary
		pParam->nMonitorCount++;
	}
	return TRUE;
}

int GetScreenWidth(void)
{
	int width, height;
	
	width = GetSystemMetrics(SM_CXSCREEN);
	height = GetSystemMetrics(SM_CYSCREEN);
	switch(width)
	{
	default:
	case 640:
	case 800:
	case 1024:
		return(width);
	case 1280:
		if(height == 480)
		{
			return(width / 2);
		}
		return(width);
	case 1600:
		if(height == 600)
		{
			return(width / 2);
		}
		return(width);
	case 2048:
		if(height == 768)
		{
			return(width / 2);
		}
		return(width);
	}
}

int GetScreenHeight(void)
{
	int width, height;
	
	width = GetSystemMetrics(SM_CXSCREEN);
	height = GetSystemMetrics(SM_CYSCREEN);
	switch(height)
	{
	default:
	case 480:
	case 600:
	case 768:
		return(height);
	case 960:
		if(width == 640)
		{
			return(height / 2);
		}
		return(height);
	case 1200:
		if(width == 800)
		{
			return(height / 2);
		}
		return(height);
	case 1536:
		if(width == 1024)
		{
			return(height / 2);
		}
		return(height);
	}
}

/*------------------------------------------------------------------*\
ID based Globals
\*------------------------------------------------------------------*/

long NewGroupID(int parentID, CString text, CString description)
{
	long lID=0;
	CTime time;
	time = CTime::GetCurrentTime();
	
	try
	{
		//sqlite doesn't like single quotes ' replace them with double ''
		if(text.IsEmpty())
			text = time.Format("NewGroup %y/%m/%d %H:%M:%S");
		text.Replace(_T("'"), _T("''"));

		CString cs;

		cs.Format(_T("insert into Main (lDate, mText, m_Description, lDontAutoDelete, bIsGroup, lParentID, stickyClipOrder, stickyClipGroupOrder) values(%d, '%s', '%s', %d, 1, %d, -(2147483647), -(2147483647));"),
							(int)time.GetTime(),
							text,
							description,
							(int)time.GetTime(),
							parentID);

		theApp.m_db.execDML(cs);

		lID = (long)theApp.m_db.lastRowId();
	}
	CATCH_SQLITE_EXCEPTION_AND_RETURN(0)
	
	return lID;
}

BOOL DeleteAllIDs()
{
	try
	{
		theApp.m_db.execDML(_T("DELETE FROM Data;"));
		theApp.m_db.execDML(_T("DELETE FROM Main;"));
	}
	CATCH_SQLITE_EXCEPTION

	return TRUE;
}

BOOL DeleteFormats(int parentID, ARRAY& formatIDs)
{	
	if(formatIDs.GetSize() <= 0)
		return TRUE;
		
	try
	{
		//Delete the requested data formats
		INT_PTR count = formatIDs.GetSize();
		for(int i = 0; i < count; i++)
		{
			int count = theApp.m_db.execDMLEx(_T("DELETE FROM Data WHERE lID = %d;"), formatIDs[i]);
		}

		CClip clip;
		if(clip.LoadFormats(parentID))
		{
			DWORD CRC = clip.GenerateCRC();

			//Update the main table with new size
			theApp.m_db.execDMLEx(_T("UPDATE Main SET CRC = %d WHERE lID = %d"), CRC, parentID);
		}
	}
	CATCH_SQLITE_EXCEPTION
		
	return TRUE;
}

CRect CenterRect(CRect startingRect)
{
	CRect crMonitor;

	HMONITOR monitorHandle = MonitorFromPoint(startingRect.TopLeft(), MONITOR_DEFAULTTONEAREST);
	if (monitorHandle == NULL)
	{
		monitorHandle = MonitorFromPoint(startingRect.TopLeft(), MONITOR_DEFAULTTOPRIMARY);
		MONITORINFO lpmi;
		lpmi.cbSize = sizeof(MONITORINFO);
		if (GetMonitorInfo(monitorHandle, &lpmi))
		{
			crMonitor.CopyRect(&lpmi.rcWork);
		}
	}
	else
	{
		MONITORINFO lpmi;
		lpmi.cbSize = sizeof(MONITORINFO);
		if (GetMonitorInfo(monitorHandle, &lpmi))
		{
			crMonitor.CopyRect(&lpmi.rcWork);
		}
	}

	return CenterRectFromRect(startingRect, crMonitor);
}

CRect CenterRectFromRect(CRect startingRect, CRect outerRect)
{
	CPoint center = outerRect.CenterPoint();

	CRect centerRect;

	centerRect.left = center.x - (startingRect.Width() / 2);
	centerRect.top = center.y - (startingRect.Height() / 2);
	centerRect.right = centerRect.left + startingRect.Width();
	centerRect.bottom = centerRect.top + startingRect.Height();

	return centerRect;
}

CRect DefaultMonitorRect()
{
	CRect crMonitor;
	CRect invalidRect(INT32_MAX, INT32_MAX, INT32_MAX, INT32_MAX);
	HMONITOR monitorHandle = MonitorFromPoint(invalidRect.TopLeft(), MONITOR_DEFAULTTOPRIMARY);
	MONITORINFO lpmi;
	lpmi.cbSize = sizeof(MONITORINFO);
	if (GetMonitorInfo(monitorHandle, &lpmi))
	{
		crMonitor.CopyRect(&lpmi.rcWork);
	}

	return crMonitor;
}

CRect MonitorRectFromRect(CRect rect)
{
	BOOL ret = FALSE;

	CRect crMonitor;

	HMONITOR monitorHandle = MonitorFromPoint(rect.TopLeft(), MONITOR_DEFAULTTONEAREST);
	if (monitorHandle == NULL)
	{
		monitorHandle = MonitorFromPoint(rect.TopLeft(), MONITOR_DEFAULTTOPRIMARY);
		MONITORINFO lpmi;
		lpmi.cbSize = sizeof(MONITORINFO);
		if (GetMonitorInfo(monitorHandle, &lpmi))
		{
			crMonitor.CopyRect(&lpmi.rcWork);
		}
	}
	else
	{
		MONITORINFO lpmi;
		lpmi.cbSize = sizeof(MONITORINFO);
		if (GetMonitorInfo(monitorHandle, &lpmi))
		{
			crMonitor.CopyRect(&lpmi.rcWork);
		}
	}

	return crMonitor;
}

BOOL EnsureWindowVisible(CRect *pcrRect)
{
	BOOL ret = FALSE;

	CRect crMonitor;

	HMONITOR monitorHandle = MonitorFromRect(pcrRect, MONITOR_DEFAULTTONEAREST);
	if (monitorHandle == NULL)
	{
		monitorHandle = MonitorFromRect(pcrRect, MONITOR_DEFAULTTOPRIMARY);
		MONITORINFO lpmi;
		lpmi.cbSize = sizeof(MONITORINFO);
		if (GetMonitorInfo(monitorHandle, &lpmi))
		{
			crMonitor.CopyRect(&lpmi.rcWork);

			*pcrRect = CenterRectFromRect(*pcrRect, crMonitor);
		}
	}
	else
	{
		MONITORINFO lpmi;
		lpmi.cbSize = sizeof(MONITORINFO);
		if (GetMonitorInfo(monitorHandle, &lpmi))
		{
			crMonitor.CopyRect(&lpmi.rcWork);
		}
	}	

	bool movedLeft = false;
	//Validate the left
	long lDiff = pcrRect->left - crMonitor.left;
	if (lDiff < 0)
	{
		pcrRect->left += abs(lDiff);
		pcrRect->right += abs(lDiff);
		ret = TRUE;
		movedLeft = true;
	}

	//Right side
	lDiff = pcrRect->right - crMonitor.right;
	if (lDiff > 0)
	{
		if (movedLeft == false)
		{
			pcrRect->left -= abs(lDiff);
		}
		pcrRect->right -= abs(lDiff);
		ret = TRUE;
	}

	bool movedTop = false;
	//Top
	lDiff = pcrRect->top - crMonitor.top;
	if (lDiff < 0)
	{
		pcrRect->top += abs(lDiff);
		pcrRect->bottom += abs(lDiff);
		ret = TRUE;
		movedTop = true;
	}

	//Bottom
	lDiff = pcrRect->bottom - crMonitor.bottom;
	if (lDiff > 0)
	{
		if (movedTop == false)
		{
			pcrRect->top -= abs(lDiff);
		}
		pcrRect->bottom -= abs(lDiff);
		ret = TRUE;
	}	

	return ret;
}

__int64 GetLastWriteTime(const CString &csFile)
{
	__int64 nLastWrite = 0;
	CFileFind finder;
	BOOL bResult = finder.FindFile(csFile);

	if (bResult)
	{
		finder.FindNextFile();

		FILETIME ft;
		finder.GetLastWriteTime(&ft);

		memcpy(&nLastWrite, &ft, sizeof(ft));
	}

	return nLastWrite;
}

typedef struct 
{
	DWORD ownerpid;
	DWORD childpid;
} windowinfo;

BOOL CALLBACK EnumChildWindowsCallback(HWND hWnd, LPARAM lp) 
{
	windowinfo* info = (windowinfo*)lp;
	DWORD pid = 0;
	GetWindowThreadProcessId(hWnd, &pid);
	if (pid != info->ownerpid) 
		info->childpid = pid;
	return TRUE;
}

CString UWP_AppName(HWND active_window, DWORD ownerpid)
{
	CString uwpAppName;
	windowinfo info = { 0 };
	info.ownerpid = ownerpid;
	info.childpid = info.ownerpid;
	EnumChildWindows(active_window, EnumChildWindowsCallback, (LPARAM)&info);
	HANDLE active_process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, info.childpid);
	if (active_process != NULL)
	{
		WCHAR image_name[MAX_PATH] = { 0 };
		DWORD bufsize = MAX_PATH;
		QueryFullProcessImageName(active_process, 0, image_name, &bufsize);
		CloseHandle(active_process);

		nsPath::CPath path(image_name);
		uwpAppName = path.GetName();
	}

	return uwpAppName;
}

CString GetProcessName(HWND hWnd, DWORD processId) 
{
	DWORD startTick = GetTickCount();

	CString	strProcessName;
	DWORD Id = processId;
	if (Id == 0)
	{		
		GetWindowThreadProcessId(hWnd, &Id);
	}

	HANDLE active_process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, Id);
	if (active_process != NULL)
	{
		WCHAR image_name[MAX_PATH] = { 0 };
		DWORD bufsize = MAX_PATH;
		QueryFullProcessImageName(active_process, 0, image_name, &bufsize);
		CloseHandle(active_process);

		nsPath::CPath path(image_name);
		strProcessName = path.GetName();
	}

	if (strProcessName == _T(""))
	{
		Log(StrF(_T("failed to get process name from open process, LastError: %d, looping over process names to find process"), GetLastError()));

		PROCESSENTRY32 processEntry = { 0 };

		HANDLE hSnapShot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		processEntry.dwSize = sizeof(PROCESSENTRY32);

		if (Process32First(hSnapShot, &processEntry))
		{
			do
			{
				if (processEntry.th32ProcessID == Id)
				{
					strProcessName = processEntry.szExeFile;
					break;
				}
			} while (Process32Next(hSnapShot, &processEntry));
		}

		CloseHandle(hSnapShot);
	}

	//uwp apps are wrapped in another app called, if this has focus then try and find the child uwp process
	if (strProcessName == _T("ApplicationFrameHost.exe"))
	{
		strProcessName = UWP_AppName(hWnd, Id);
	}

	DWORD endTick = GetTickCount();
	DWORD diff = endTick - startTick;
	if(diff > 5)
	{
		Log(StrF(_T("GetProcessName Time (ms): %d, pid: %d, name: %s"), endTick-startTick, Id, strProcessName));
	}

	return strProcessName;
}

BOOL IsVista()
{
	return TRUE;
}

void DeleteDittoTempFiles(BOOL checkFileLastAccess)
{
	CString csDir = CGetSetOptions::GetPath(PATH_REMOTE_FILES);
	if (FileExists(csDir))
	{
		DeleteFolderFiles(csDir, checkFileLastAccess, CTimeSpan(0, 1, 0, 0));
	}

	csDir = CGetSetOptions::GetPath(PATH_DRAG_FILES);
	if (FileExists(csDir))
	{
		DeleteFolderFiles(csDir, checkFileLastAccess, CTimeSpan(0, 1, 0, 0));
	}

	csDir = CGetSetOptions::GetPath(PATH_CLIP_DIFF);
	if (FileExists(csDir))
	{
		DeleteFolderFiles(csDir, checkFileLastAccess, CTimeSpan(0, 1, 0, 0));
	}
}

void DeleteFolderFiles(CString csDir, BOOL checkFileLastAccess, CTimeSpan lastAccessOffset)
{
	if (csDir.Find(_T("\\ReceivedFiles\\")) == -1 && csDir.Find(_T("\\DragFiles\\")) == -1 && csDir.Find(_T("ClipCompare")) == -1 && csDir.Find(_T("EditClips")) == -1)
		return;

	Log(StrF(_T("Deleting files in Folder %s Check Last Access %d"), csDir, checkFileLastAccess));

	FIX_CSTRING_PATH(csDir);

	CTime ctOld = CTime::GetCurrentTime();
	CTime ctFile;
	ctOld -= lastAccessOffset;

	CFileFind Find;

	CString csFindString;
	csFindString.Format(_T("%s*.*"), csDir);

	BOOL bFound = Find.FindFile(csFindString);
	while(bFound)
	{
		bFound = Find.FindNextFile();

		if(Find.IsDots())
			continue;

		if(checkFileLastAccess &&
			Find.GetLastAccessTime(ctFile))
		{
			//Delete the remote copied file if it hasn't been used for the last day
			if(ctFile < ctOld)
			{
				Log(StrF(_T("Deleting temp file %s"), Find.GetFilePath()));
				DeleteFile(Find.GetFilePath());
			}
		}
		else
		{
			Log(StrF(_T("Deleting temp file %s"), Find.GetFilePath()));
			DeleteFile(Find.GetFilePath());
		}
	}
}

__int64 FileSize(const TCHAR *fileName)
{
	struct _stat64  buf;
	if (_wstat64((wchar_t const*)fileName, &buf) != 0)
		return -1; // error, could use errno to find out more

	return buf.st_size;
}

int FindNoCaseAndInsert(CString& mainStr, CString& findStr, CString preInsert, CString postInsert, int linesPerRow)
{
	int replaceCount = 0;

	//Prevent infinite loop when user tries to replace nothing.
	if (findStr != "")
	{
		int oldLen = findStr.GetLength();

		int foundPos = 0;
		int startFindPos = 0;
		int newPos = 0;
		int insertedLength = 0;

		int firstFindPos = 0;

		CString mainLow(theApp.m_icuString.ToLowerStringEx(mainStr));		
		CString findLow(theApp.m_icuString.ToLowerStringEx(findStr));
		findLow.MakeLower();
		
		int preLength = preInsert.GetLength();
		int postLength = postInsert.GetLength();

		int x = mainLow.Find(findLow, 0);
		
		while(TRUE)
		{
			foundPos = mainLow.Find(findLow, startFindPos);
			if (foundPos < 0)
				break;

			if (replaceCount == 0)
			{
				firstFindPos = foundPos + preLength;
			}

			newPos = foundPos + insertedLength;

			mainStr.Insert(newPos, preInsert);
			mainStr.Insert(newPos + preLength + oldLen, postInsert);

			startFindPos = foundPos + oldLen;

			insertedLength += preLength + postLength;

			replaceCount++;

			//safety check, make sure we don't look forever
			if (replaceCount > 100)
				break;
		}

		if(replaceCount > 0)
		{
			TruncateTextToMatchLine(mainStr, firstFindPos, linesPerRow);
		}
	}

	return replaceCount;
}

void TruncateTextToMatchLine(CString& mainStr, int firstMatchPos, int linesPerRow)
{
	int startFindPos = 0;
	int line = 0;
	int prevLinePos = 0;
	int prevPrevLinePos = 0;

	while (TRUE)
	{
		int foundPos = mainStr.Find(_T("\n"), startFindPos);
		if (foundPos < 0)
			break;

		if (firstMatchPos < foundPos)
		{
			if (line > linesPerRow - 1)
			{
				int lineStart = prevLinePos;
				if (linesPerRow > 1)
				{
					lineStart = prevPrevLinePos;
				}

				mainStr = _T("... ") + mainStr.Mid(lineStart + 1);
			}

			break;
		}

		startFindPos = foundPos + 1;
		prevPrevLinePos = prevLinePos;
		prevLinePos = foundPos;

		line++;

		if (line > 1000)
			break;
	}

	mainStr.Replace(_T("\r\n"), _T("\x01\x05\x02"));
	mainStr.Replace(_T("\r"), _T("\x01\x05\x02"));
	mainStr.Replace(_T("\n"), _T("\x01\x05\x02"));
}

void OnInitMenuPopupEx(CMenu *pPopupMenu, UINT nIndex, BOOL bSysMenu, CWnd *pWnd)
{
	ASSERT(pPopupMenu != NULL);
	// Check the enabled state of various menu items.

	CCmdUI state;
	state.m_pMenu = pPopupMenu;
	ASSERT(state.m_pOther == NULL);
	ASSERT(state.m_pParentMenu == NULL);

	// Determine if menu is popup in top-level menu and set m_pOther to
	// it if so (m_pParentMenu == NULL indicates that it is secondary popup).
	HMENU hParentMenu;
	if (AfxGetThreadState()->m_hTrackingMenu == pPopupMenu->m_hMenu)
	{
		state.m_pParentMenu = pPopupMenu;    // Parent == child for tracking popup.
	}
	else if ((hParentMenu = ::GetMenu(pWnd->m_hWnd)) != NULL)
	{
		CWnd* pParent = pWnd;
		// Child windows don't have menus--need to go to the top!
		if (pParent != NULL &&
			(hParentMenu = ::GetMenu(pParent->m_hWnd)) != NULL)
		{
			int nIndexMax = ::GetMenuItemCount(hParentMenu);
			for (int nIndex = 0; nIndex < nIndexMax; nIndex++)
			{
				if (::GetSubMenu(hParentMenu, nIndex) == pPopupMenu->m_hMenu)
				{
					// When popup is found, m_pParentMenu is containing menu.
					state.m_pParentMenu = CMenu::FromHandle(hParentMenu);
					break;
				}
			}
		}
	}

	state.m_nIndexMax = pPopupMenu->GetMenuItemCount();
	for (state.m_nIndex = 0; state.m_nIndex < state.m_nIndexMax;
		state.m_nIndex++)
	{
		state.m_nID = pPopupMenu->GetMenuItemID(state.m_nIndex);
		if (state.m_nID == 0)
			continue; // Menu separator or invalid cmd - ignore it.

		ASSERT(state.m_pOther == NULL);
		ASSERT(state.m_pMenu != NULL);
		if (state.m_nID == (UINT)-1)
		{
			// Possibly a popup menu, route to first item of that popup.
			state.m_pSubMenu = pPopupMenu->GetSubMenu(state.m_nIndex);
			if (state.m_pSubMenu == NULL ||
				(state.m_nID = state.m_pSubMenu->GetMenuItemID(0)) == 0 ||
				state.m_nID == (UINT)-1)
			{
				continue;       // First item of popup can't be routed to.
			}
			state.DoUpdate(pWnd, TRUE);   // Popups are never auto disabled.
		}
		else
		{
			// Normal menu item.
			// Auto enable/disable if frame window has m_bAutoMenuEnable
			// set and command is _not_ a system command.
			state.m_pSubMenu = NULL;
			state.DoUpdate(pWnd, FALSE);
		}

		// Adjust for menu deletions and additions.
		UINT nCount = pPopupMenu->GetMenuItemCount();
		if (nCount < state.m_nIndexMax)
		{
			state.m_nIndex -= (state.m_nIndexMax - nCount);
			while (state.m_nIndex < nCount &&
				pPopupMenu->GetMenuItemID(state.m_nIndex) == state.m_nID)
			{
				state.m_nIndex++;
			}
		}
		state.m_nIndexMax = nCount;
	}
} 

CString InternetEncode(CString text)
{
	CString ret = _T("");
	LPTSTR lpOutputBuffer = new TCHAR[1];
	DWORD dwSize = 1;
	BOOL fRes = ::InternetCanonicalizeUrl(text, lpOutputBuffer, &dwSize, ICU_BROWSER_MODE);
	DWORD dwError = ::GetLastError();
	if (!fRes && dwError == ERROR_INSUFFICIENT_BUFFER)
	{
		delete lpOutputBuffer;
		lpOutputBuffer = new TCHAR[dwSize];
		fRes = ::InternetCanonicalizeUrl(text, lpOutputBuffer, &dwSize, ICU_BROWSER_MODE);
		if (fRes)
		{
			ret = lpOutputBuffer;
			//lpOutputBuffer has decoded url
		}
		else
		{
			//failed to decode
		}
		if (lpOutputBuffer != NULL)
		{
			delete [] lpOutputBuffer;
			lpOutputBuffer = NULL;
		}
	}
	else
	{
		//some other error OR the input string url is just 1 char and was successfully decoded
	}

	return ret;
}

void DeleteParamFromRTF(CStringA &test, CStringA find, bool searchForTrailingDigits)
{
	int start = 0;

	while (start >= 0)
	{
		start = test.Find(find, start);
		if (start >= 0)
		{
			if (start > 0)
			{
				//leave it if the preceding character is \\, i was seeing the double slash if the actual text contained slash
				if (test[start - 1] == '\\')
				{
					start++;
					continue;
				}
			}

			int end = -1;	
			int innerStart = start + find.GetLength();

			if (searchForTrailingDigits)
			{
				for (int i = innerStart; i < test.GetLength(); i++)
				{
					if (isdigit(test[i]) == false)
					{
						end = i;
						break;
					}
				}
			}
			else
			{
				end = innerStart;
			}

			if (end > 0)
			{
				if (searchForTrailingDigits == false ||
					end != innerStart)
				{
					test.Delete(start, (end - start));
				}
				else
				{
					start++;
				}
			}
			else
			{
				break;
			}
		}
	}
}

bool RemoveRTFSection(CStringA &str, CStringA section)
{
	bool removedSection = false;

	int start2 = str.Find(section, 0);
	int end2 = 0;
	if (start2 >= 0)
	{
		int in = 0;
		for (int pos = start2+1; pos < str.GetLength(); pos++)
		{
			if (str[pos] == '{')
			{
				in++;
			}

			if (str[pos] == '}')
			{
				if (in > 0)
				{
					in--;
				}
				else
				{
					end2 = pos;
					break;
				}
			}
		}

		if (end2 > start2)
		{
			str.Delete(start2, (end2 - start2) + 1);
			removedSection = true;
		}
	}

	return removedSection;
}

CString NewGuidString()
{
	CString guidString;

	GUID guid;
	CoCreateGuid(&guid);
	guidString.Format(_T("%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX"),
		guid.Data1, guid.Data2, guid.Data3,
		guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
		guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);

	return guidString.MakeLower();
}

CString FolderPath(int folderId)
{
	CString folder = _T("");
	if (folderId > 0)
	{
		try
		{
			CStringArray arr;
			for (int i = 0; i < 100; i++)
			{
				CppSQLite3Query parent = theApp.m_db.execQueryEx(_T("SELECT lID, mText, lParentID FROM Main WHERE lID = %d"), folderId);
				if (parent.eof() == false)
				{
					arr.Add(parent.getStringField(_T("mText")));
					folderId = parent.getIntField(_T("lParentID"));
				}
				else
				{
					break;
				}
			}

			folder = _T("Group Path: \\");
			for (INT_PTR folderPos = arr.GetCount() - 1; folderPos >= 0; folderPos--)
			{
				folder += _T("\\");
				folder += arr[folderPos];
			}
		}
		CATCH_SQLITE_EXCEPTION
	}

	return folder;
}

BOOL DarkAppWindows10Setting()
{
	BOOL darkMode = false;
	HKEY hkKey;
	long lResult = ::RegOpenKeyEx(HKEY_CURRENT_USER, _T("Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"), NULL, KEY_READ, &hkKey);
	if (lResult == ERROR_SUCCESS)
	{
		DWORD buffer;
		DWORD len = sizeof(buffer);
		DWORD type;

		lResult = ::RegQueryValueEx(hkKey, _T("AppsUseLightTheme"), 0, &type, (LPBYTE)&buffer, &len);

		if (lResult == ERROR_SUCCESS)
		{
			darkMode = (buffer == 0);
		}

		RegCloseKey(hkKey);
	}

	return darkMode;
}

DWORD Windows10AccentColor()
{
	DWORD color = -1;
	BOOL darkMode = false;
	HKEY hkKey;
	long lResult = ::RegOpenKeyEx(HKEY_CURRENT_USER, _T("Software\\Microsoft\\Windows\\DWM"), NULL, KEY_READ, &hkKey);
	if (lResult == ERROR_SUCCESS)
	{
		DWORD buffer;
		DWORD len = sizeof(buffer);
		DWORD type;

		lResult = ::RegQueryValueEx(hkKey, _T("ColorizationColor"), 0, &type, (LPBYTE)&buffer, &len);

		if (lResult == ERROR_SUCCESS)
		{
			color = buffer;
		}

		RegCloseKey(hkKey);
	}

	return color;
}

BOOL RestoreDbPrompt(HWND hwnd)
{
	BOOL ret = false;

	OPENFILENAME ofn;
	TCHAR szFile[400];
	TCHAR szDir[400];

	memset(&szFile, 0, sizeof(szFile));
	memset(szDir, 0, sizeof(szDir));
	memset(&ofn, 0, sizeof(ofn));

	ofn.lStructSize = sizeof(OPENFILENAME);
	ofn.hwndOwner = hwnd;
	ofn.lpstrFile = szFile;
	ofn.nMaxFile = sizeof(szFile);
	ofn.lpstrFilter = _T("Ditto database backups (.zdb)\0*.zdb\0\0");
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	//ofn.lpstrInitialDir = szDir;
	ofn.lpstrDefExt = _T("zdb");
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

	if (GetOpenFileName(&ofn))
	{
		CWaitCursor wait;

		CString dbPath = CGetSetOptions::GetDBPath();
		CString backupPath(ofn.lpstrFile);
		ret = RestoreDB(backupPath);
	}

	return ret;
}

BOOL BackupDbPrompt(HWND hwnd)
{
	BOOL ret = FALSE;

	OPENFILENAME ofn;
	TCHAR szFile[400];
	TCHAR szDir[400];

	memset(&szFile, 0, sizeof(szFile));
	memset(szDir, 0, sizeof(szDir));
	memset(&ofn, 0, sizeof(ofn));

	ofn.lStructSize = sizeof(OPENFILENAME);
	ofn.hwndOwner = hwnd;
	ofn.lpstrFile = szFile;
	ofn.nMaxFile = sizeof(szFile);
	ofn.lpstrFilter = _T("Ditto database backups (.zdb)\0*.zdb\0\0");
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	//ofn.lpstrInitialDir = szDir;
	ofn.lpstrDefExt = _T("zdb");
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

	if (GetSaveFileName(&ofn))
	{
		CWaitCursor wait;

		CString dbPath = CGetSetOptions::GetDBPath();
		CString backupPath(ofn.lpstrFile);
		theApp.m_db.execDML(_T("PRAGMA wal_checkpoint(TRUNCATE);"));
		ret = BackupDB(dbPath, backupPath);
	}

	return ret;
}

int WordCount(const CString &text)
{
	enum { STATE_OUT = 0, STATE_IN = 1 };

	int state = STATE_OUT;
	unsigned wc = 0; // word count

	// Scan all characters one by one
	for(int pos = 0; pos < text.GetLength(); pos++)	
	{
		auto str = text[pos];
		
		if (str == ' ' || str == '\r' || str == '\n' || str == '\t')
		{
			state = STATE_OUT;
		}
		else if (state == STATE_OUT)
		{
			state = STATE_IN;
			wc++;
		}
	}

	return wc;
}

CString GetVersionString(VersionInfo version)
{
	CString csLine;
	csLine.Format(_T("%02i.%02i.%02i.%02i"),
		version.Major,
		version.Minor,
		version.Revision,
		version.Build);

	return csLine;
}

VersionInfo GetRunningVersion()
{
	VersionInfo verInfo;
	CString csFileName = CGetSetOptions::GetExeFileName();

	DWORD dwSize, dwHandle;
	LPBYTE lpData;
	UINT iBuffSize;
	VS_FIXEDFILEINFO* lpFFI;

	dwSize = GetFileVersionInfoSize(csFileName.GetBuffer(csFileName.GetLength()), &dwHandle);

	if (dwSize != 0)
	{
		csFileName.ReleaseBuffer();
		if ((lpData = (unsigned char*)malloc(dwSize)) != NULL)
		{
			if (GetFileVersionInfo(csFileName.GetBuffer(csFileName.GetLength()), dwHandle, dwSize, lpData) != 0)
			{
				if (VerQueryValue(lpData, _T("\\"), (LPVOID*)&lpFFI, &iBuffSize) != 0)
				{
					if (iBuffSize > 0)
					{
						verInfo.Major = (lpFFI->dwProductVersionMS >> 16) & 0xffff;
						verInfo.Minor = (lpFFI->dwProductVersionMS >> 0) & 0xffff;
						verInfo.Revision = (lpFFI->dwProductVersionLS >> 16) & 0xffff;
						verInfo.Build = (lpFFI->dwProductVersionLS >> 0) & 0xffff;
					}
				}
			}
			free(lpData);
		}
	}

	return(verInfo);
}