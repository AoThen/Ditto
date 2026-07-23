#pragma once

#ifdef _UNICODE				
	#define STRLEN(pText)	wcslen(pText)
#else
	#define STRLEN(pText)	strlen(pText)
#endif

#ifdef _UNICODE				
	#define STRSTR(a, b)	wcsstr(a, b)
#else
	#define STRSTR(a, b)	strstr(a, b)
#endif


#ifdef _UNICODE				
	#define STRCMP(a, b)	wcscmp(a, b)
#else
	#define STRCMP(a, b)	strcmp(a, b)
#endif

#ifdef _UNICODE				
	#define STRCPY_S(a, sz, b)	wcscpy_s(a, sz, b)
#else
	#define STRCPY_S(a, sz, b)	strcpy_s(a, sz, b)
#endif

#ifdef _UNICODE				
	#define STRNCPY_S(a, sz, b, l)	wcsncpy_s(a, sz, b, l)
#else
	#define STRNCPY_S(a, sz, b, l)	strncpy_s(a, sz, b, l)
#endif

#ifdef _UNICODE
	#define SPRINTF	wsprintf
#else
	#define SPRINTF	sprintf
#endif

#ifdef _UNICODE				
	#define ATOL(a)	_wtol(a)
#else
	#define ATOL(a)	atol(a)
#endif

#ifdef _UNICODE				
	#define ATOI(a)	_wtoi(a)
#else
	#define ATOI(a)	atoi(a)
#endif

#ifdef _UNICODE				
	#define SPLITPATH	_wsplitpath
#else
	#define SPLITPATH	_splitpath
#endif

#ifdef _UNICODE
	#define STAT	_wstat
#else
	#define STAT	_stat
#endif

#ifdef _UNICODE
#define STRICMP	_wcsicmp
#else
#define STRICMP	_stricmp
#endif

#ifdef _UNICODE
#define GETENV _wgetenv
#else
#define GETENV getenv
#endif
