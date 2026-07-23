#pragma once

#include "ClipboardViewer.h"
#include <afxmt.h>
#include <atomic>
#include <memory>

struct CCopyConfig
{
public:
	// WM_CLIPBOARD_COPIED is sent to this window when a copy is made.
	HWND        m_hClipHandler;
	// true to use PostMessage (asynchronous)
	// false to use SendMessage (synchronous)
	bool        m_bAsyncCopy;
	// true to create a copy of the clipboard contents when it changes
	// false to ignore changes in the clipboard
	bool        m_bCopyOnChange;
	// the supported types which are copied from the clipboard when it changes.
	std::unique_ptr<CClipTypes> m_pSupportedTypes;

	CCopyConfig( HWND hClipHandler = NULL,
	             bool bAsyncCopy = false,
				 bool bCopyOnChange = false,
				 CClipTypes* pSupportedTypes = NULL )
		: m_hClipHandler(hClipHandler)
		, m_bAsyncCopy(bAsyncCopy)
		, m_bCopyOnChange(bCopyOnChange)
		, m_pSupportedTypes(pSupportedTypes)
	{
	}

	// Move constructor
	CCopyConfig(CCopyConfig&& other) noexcept
		: m_hClipHandler(other.m_hClipHandler)
		, m_bAsyncCopy(other.m_bAsyncCopy)
		, m_bCopyOnChange(other.m_bCopyOnChange)
		, m_pSupportedTypes(std::move(other.m_pSupportedTypes))
	{
	}

	// Move assignment
	CCopyConfig& operator=(CCopyConfig&& other) noexcept
	{
		if (this != &other)
		{
			m_hClipHandler = other.m_hClipHandler;
			m_bAsyncCopy = other.m_bAsyncCopy;
			m_bCopyOnChange = other.m_bCopyOnChange;
			m_pSupportedTypes = std::move(other.m_pSupportedTypes);
		}
		return *this;
	}

	// Delete copy constructor and copy assignment to prevent shallow copy of unique_ptr
	CCopyConfig(const CCopyConfig&) = delete;
	CCopyConfig& operator=(const CCopyConfig&) = delete;
};

class CCopyThread : public CWinThread
{
	DECLARE_DYNCREATE(CCopyThread)
public:
	CCopyThread();
	virtual ~CCopyThread();

// Attributes
public:

// Operations
public:

	bool m_bQuit;
	bool m_connectOnStartup;

	CCriticalSection m_cs;

	// CopyThread Local (accessed from this CopyThread)
	// window owned by this thread which handles clipboard viewer messages
	CClipboardViewer*   m_pClipboardViewer; // permanent during lifetime of thread
	CCopyConfig         m_LocalConfig;

	// Called within Copy Thread:
	void OnClipboardChange(CString activeWindow, CString activeWindowTitle); // called by ClipboardViewer
	void SyncConfig(); // safely syncs m_LocalConfig with m_SharedConfig

// Shared (use thread-safe access functions below)
	CCopyConfig         m_SharedConfig;
	std::atomic<bool>   m_bConfigChanged; // true if m_SharedConfig was changed.

	// Called within Main thread:
	bool IsClipboardViewerConnected();
	bool GetConnectCV();
	void SetConnectCV(bool bConnect);

	void SetSupportedTypes(CClipTypes* pTypes); // CopyThread will own pTypes
	HWND SetClipHandler(HWND hWnd); // returns previous value
	HWND GetClipHandler();
	bool SetCopyOnChange(bool bVal); // returns previous value
	bool GetCopyOnChange();
	bool SetAsyncCopy(bool bVal); // returns previous value
	bool GetAsyncCopy();

	void Init(CCopyConfig cfg);
	bool Quit();

	virtual BOOL InitInstance();
	virtual int ExitInstance();
};
