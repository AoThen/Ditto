#include "stdafx.h"
#include "CloudSyncManager.h"
#include "CloudAuth.h"
#include "CloudCrypto.h"
#include "CloudEncryption.h"
#include "../httplib.h"
#include "../json.hpp"
#include "../Options.h"
#include "../Clip.h"
#include "../sqlite/CppSQLite3.h"
#include "../CP_Main.h"
#include <set>

using json = nlohmann::json;

const int CCloudSyncManager::CLOUD_PUSH_BATCH_SIZE = 200;

// Helper: convert CString to std::string (with null safety)
static std::string CStringToStdString(const CString& str)
{
	if (str.IsEmpty())
		return std::string();
	CT2A utf8(str, CP_UTF8);
	if (utf8.m_psz == nullptr)
		return std::string();
	return std::string(utf8.m_psz);
}

static CString GetCurrentTimeStamp()
{
	CTime now = CTime::GetCurrentTime();
	return now.Format(_T("%Y-%m-%d %H:%M:%S"));
}

static void LogMessage(const CString& msg)
{
	CString logMsg;
	logMsg.Format(_T("[CloudSync] %s %s\n"), GetCurrentTimeStamp(), msg.GetString());
	OutputDebugString(logMsg);
}

void CCloudSyncManager::EnsureHttpClient()
{
	EnterCriticalSection(&m_csHttpClient);
	CStringA serverUrlA(m_serverUrl);
	std::string url = serverUrlA.GetString();
	if (!m_httpClient || m_httpClientUrl != m_serverUrl)
	{
		m_httpClient = std::make_unique<httplib::Client>(url);
		m_httpClient->set_connection_timeout(10, 0);
		m_httpClient->set_read_timeout(30, 0);
		m_httpClient->set_write_timeout(30, 0);
		m_httpClient->set_default_headers({
			{"Authorization", "Bearer " + std::string(CStringA(m_deviceToken))}
		});
		m_httpClientUrl = m_serverUrl;
	}
	LeaveCriticalSection(&m_csHttpClient);
}

BOOL CCloudSyncManager::IsEncryptionExpected()
{
	return CGetSetOptions::GetCloudSyncEncryptionEnabled();
}

CCloudSyncManager::CCloudSyncManager()
	: m_hStopEvent(nullptr)
	, m_hWsTrigger(nullptr)
	, m_pSyncThread(nullptr)
	, m_pWsThread(nullptr)
	, m_cryptoInitialized(FALSE)
	, m_lastSyncTime(0)
	, m_nActiveQuickSyncThreads(0)
	, m_bFirstPushInProgress(0)
	, m_pWsClient(nullptr)
	, m_lastPushTime(0)
	, m_wsReconnectDelay(1000)
	, m_pEncRetryThread(nullptr)
	, m_hEncRetryStop(nullptr)
	, m_forceOverrideLocal(0)
	, m_forceOverrideRemote(0)
	, m_lastSyncSuccessTime(0)
{
	InitializeCriticalSection(&m_csSync);
	InitializeCriticalSection(&m_csHttpClient);
	InitializeCriticalSection(&m_csWsClient);
	InitializeCriticalSection(&m_csStatus);
}

CCloudSyncManager::~CCloudSyncManager()
{
	Stop();
	DeleteCriticalSection(&m_csSync);
	DeleteCriticalSection(&m_csHttpClient);
	DeleteCriticalSection(&m_csWsClient);
	DeleteCriticalSection(&m_csStatus);
}

BOOL CCloudSyncManager::Initialize()
{
	m_serverUrl = CGetSetOptions::GetCloudServerUrl();
	if (m_serverUrl.IsEmpty())
	{
		m_serverUrl = CLOUD_DEFAULT_SERVER_URL;
	}
	m_deviceToken = CGetSetOptions::GetCloudDeviceToken();
	m_deviceId = CGetSetOptions::GetCloudDeviceId();

	// Restore lastSyncTime from registry (persisted across restarts)
	m_lastSyncTime = (time_t)CGetSetOptions::GetCloudLastSyncTime();
	if (m_lastSyncTime > 0)
	{
		CString msg;
		CTime lastSync(m_lastSyncTime);
		msg.Format(_T("Restored lastSyncTime from registry: %s"), lastSync.Format(_T("%Y-%m-%d %H:%M:%S")));
		LogMessage(msg);
	}

	// Restore lastPushTime from registry (separate push cursor)
	m_lastPushTime = (time_t)CGetSetOptions::GetCloudLastPushTime();
	if (m_lastPushTime == 0)
		m_lastPushTime = m_lastSyncTime;

	// If no device ID is stored, generate one (will be overwritten on login)
	if (m_deviceId.IsEmpty())
	{
		// Generate a temporary device ID from computer name
		TCHAR szComputerName[MAX_COMPUTERNAME_LENGTH + 1];
		DWORD dwSize = ARRAYSIZE(szComputerName);
		if (GetComputerName(szComputerName, &dwSize))
		{
			CStringA tempId;
			CT2A computerNameA(szComputerName, CP_UTF8);
			tempId.Format("device-%s", (LPCSTR)computerNameA);
			m_deviceId = tempId;
		}
		else
		{
			m_deviceId = "device-unknown";
		}
	}

	if (!CCloudAuth::IsLoggedIn())
	{
		OutputDebugString(_T("[CloudSync] Not logged in, skipping sync initialization.\n"));
		return FALSE;
	}

	// Ensure remote ID mapping table exists (M1)
	EnsureMappingTable();

	// Initialize encryption (best effort, log warning if fails)
	if (!InitializeEncryption())
	{
		if (IsEncryptionExpected())
		{
			// User has encryption enabled but initialization failed — abort
			LogMessage(_T("CRITICAL: Encryption is enabled but failed to initialize. Aborting sync initialization."));

			// Set persistent flag so OptionCloud can show recovery prompt even if 997 message is lost
			CGetSetOptions::SetCloudEncryptionNeedsRecovery(TRUE);

			CWnd* pMainWnd = AfxGetMainWnd();
			if (pMainWnd != nullptr)
			{
				::PostMessage(pMainWnd->GetSafeHwnd(), WM_CLOUD_AUTH_REQUIRED, 997, 0);
			}

			// Start background retry with exponential backoff (max 12 attempts, ~2h window)
			StartEncryptionRetry();
			return FALSE;
		}
		else
		{
			// User did not enable encryption — continue without it (degraded mode)
			OutputDebugString(_T("[CloudSync] WARNING: Encryption initialization failed, continuing without encryption.\n"));
			
			// Show user-friendly warning via main window
			CWnd* pMainWnd = AfxGetMainWnd();
			if (pMainWnd != nullptr)
			{
				// Post custom message to notify encryption issue
				::PostMessage(pMainWnd->GetSafeHwnd(), WM_CLOUD_AUTH_REQUIRED, 999, 0);
			}
			
			LogMessage(_T("WARNING: Encryption not initialized, clips will sync unencrypted."));
		}
	}

	// Create stop event
	m_hStopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	if (m_hStopEvent == nullptr)
	{
		OutputDebugString(_T("[CloudSync] Failed to create stop event.\n"));
		return FALSE;
	}

	// Create WS trigger event
	m_hWsTrigger = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	if (m_hWsTrigger == nullptr)
	{
		OutputDebugString(_T("[CloudSync] Failed to create WS trigger event.\n"));
		CloseHandle(m_hStopEvent);
		m_hStopEvent = nullptr;
		return FALSE;
	}

	// Create sync thread (suspended, then resumed)
	m_pSyncThread = AfxBeginThread(SyncThreadProc, this, THREAD_PRIORITY_NORMAL, 0, CREATE_SUSPENDED);
	if (m_pSyncThread == nullptr)
	{
		OutputDebugString(_T("[CloudSync] Failed to create sync thread.\n"));
		CloseHandle(m_hWsTrigger);
		m_hWsTrigger = nullptr;
		CloseHandle(m_hStopEvent);
		m_hStopEvent = nullptr;
		return FALSE;
	}

	m_pSyncThread->m_bAutoDelete = FALSE;
	m_pSyncThread->ResumeThread();

	// Start WebSocket listener thread (H4)
	StartWebSocket();

	LogMessage(_T("Initialized successfully."));
	return TRUE;
}

BOOL CCloudSyncManager::ReinitializeSync()
{
	LogMessage(_T("ReinitializeSync: stopping and restarting sync..."));

	// Stop cleans up events, sync thread, WS thread (safe even if nothing was created)
	Stop();

	// Reset crypto flag so InitializeEncryption re-reads from registry
	EnterCriticalSection(&m_csSync);
	m_cryptoInitialized = FALSE;
	LeaveCriticalSection(&m_csSync);

	// Re-run full init — reads settings, creates events, thread, WS
	return Initialize();
}

// ---------------------------------------------------------------------------
// Encryption retry: background thread with exponential backoff
// Triggered when encryption is expected but DEK is lost at startup.
// Retries up to 12 times (~2 hour window), then stops permanently.
// ---------------------------------------------------------------------------
void CCloudSyncManager::StartEncryptionRetry()
{
	if (m_pEncRetryThread != nullptr)
	{
		LogMessage(_T("StartEncryptionRetry: retry thread already running, skipping"));
		return;
	}

	m_hEncRetryStop = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	if (m_hEncRetryStop == nullptr)
	{
		LogMessage(_T("StartEncryptionRetry: failed to create stop event"));
		return;
	}

	m_pEncRetryThread = AfxBeginThread(EncryptionRetryThreadProc, this,
		THREAD_PRIORITY_BELOW_NORMAL, 0, 0);
	if (m_pEncRetryThread == nullptr)
	{
		LogMessage(_T("StartEncryptionRetry: failed to create thread"));
		CloseHandle(m_hEncRetryStop);
		m_hEncRetryStop = nullptr;
		return;
	}

	m_pEncRetryThread->m_bAutoDelete = FALSE;
	LogMessage(_T("StartEncryptionRetry: retry thread started"));
}

void CCloudSyncManager::StopEncryptionRetry()
{
	if (m_hEncRetryStop != nullptr)
	{
		SetEvent(m_hEncRetryStop);
	}

	if (m_pEncRetryThread != nullptr)
	{
		DWORD dwWait = WaitForSingleObject(m_pEncRetryThread->m_hThread, 5000);
		if (dwWait == WAIT_TIMEOUT)
		{
			LogMessage(_T("StopEncryptionRetry: retry thread did not exit within 5s"));
		}
		delete m_pEncRetryThread;
		m_pEncRetryThread = nullptr;
	}

	if (m_hEncRetryStop != nullptr)
	{
		CloseHandle(m_hEncRetryStop);
		m_hEncRetryStop = nullptr;
	}
}

UINT CCloudSyncManager::EncryptionRetryThreadProc(LPVOID pParam)
{
	CCloudSyncManager* pThis = static_cast<CCloudSyncManager*>(pParam);
	if (pThis == nullptr)
		return 1;

	LogMessage(_T("EncryptionRetryThreadProc: started"));

	// Exponential backoff: 30s, 60s, 120s, 240s, 480s, 600s (cap), ...
	const DWORD baseDelay = 30 * 1000;
	const DWORD maxDelay  = 10 * 60 * 1000;
	const int   maxRetries = 12;

	for (int attempt = 1; attempt <= maxRetries; attempt++)
	{
		// Calculate delay: 30s × 2^(attempt-1), capped at 10min
		DWORD delay;
		if (attempt == 1)
			delay = baseDelay;
		else
			delay = min(baseDelay * (1 << (attempt - 1)), maxDelay);

		// Wait for delay or stop signal
		DWORD dwWait = WaitForSingleObject(pThis->m_hEncRetryStop, delay);
		if (dwWait == WAIT_OBJECT_0)
		{
			LogMessage(_T("EncryptionRetryThreadProc: stop signaled, exiting"));
			return 0;
		}

		CString msg;
		msg.Format(_T("EncryptionRetryThreadProc: attempt %d/%d"), attempt, maxRetries);
		LogMessage(msg);

		if (pThis->InitializeEncryption())
		{
			LogMessage(_T("EncryptionRetryThreadProc: encryption recovery succeeded"));

			// Clear persistent recovery flag
			CGetSetOptions::SetCloudEncryptionNeedsRecovery(FALSE);

			// Post reinit request to main thread to avoid deadlock
			// (ReinitializeSync calls Stop which waits on this thread)
			CWnd* pMainWnd = AfxGetMainWnd();
			if (pMainWnd != nullptr)
			{
				::PostMessage(pMainWnd->GetSafeHwnd(), WM_CLOUD_REINIT_SYNC, 0, 0);
			}
			return 0;
		}

		LogMessage(_T("EncryptionRetryThreadProc: attempt failed, will retry"));
	}

	CString msg;
	msg.Format(_T("EncryptionRetryThreadProc: all %d attempts exhausted, giving up"), maxRetries);
	LogMessage(msg);
	return 1;
}

void CCloudSyncManager::Stop()
{
	StopEncryptionRetry();

	if (m_hStopEvent != nullptr)
	{
		SetEvent(m_hStopEvent);
	}

	if (m_pSyncThread != nullptr)
	{
		// Wait up to 15 seconds for thread to exit gracefully
		// DO NOT use TerminateThread -- it can corrupt SQLite database and leak resources
		DWORD dwWait = WaitForSingleObject(m_pSyncThread->m_hThread, 15000);
		if (dwWait == WAIT_TIMEOUT)
		{
			// Thread didn't exit in time -- log warning and continue cleanup
			// The thread will eventually exit when it checks m_hStopEvent
			OutputDebugString(_T("[CloudSync] WARNING: Sync thread did not exit within timeout, continuing cleanup.\n"));
		}
		else
		{
			OutputDebugString(_T("[CloudSync] Sync thread exited cleanly.\n"));
		}

		delete m_pSyncThread;
		m_pSyncThread = nullptr;
	}

	// Wait for all active quick-push threads to complete (up to 5 seconds)
	// This prevents use-after-free if the manager is destroyed while threads are running
	DWORD waitStart = GetTickCount();
	while (m_nActiveQuickSyncThreads > 0)
	{
		Sleep(50);
		if (GetTickCount() - waitStart > 5000)
		{
			OutputDebugStringA("[CloudSync] WARNING: Timeout waiting for quick-push threads to complete.\n");
			break;
		}
	}
	if (m_nActiveQuickSyncThreads == 0)
	{
		OutputDebugStringA("[CloudSync] All quick-push threads completed.\n");
	}

	// Stop WebSocket thread (H4)
	StopWebSocket();

	if (m_hWsTrigger != nullptr)
	{
		CloseHandle(m_hWsTrigger);
		m_hWsTrigger = nullptr;
	}

	if (m_hStopEvent != nullptr)
	{
		CloseHandle(m_hStopEvent);
		m_hStopEvent = nullptr;
	}
}

void CCloudSyncManager::OnClipAdded(void* pClip)
{
	UNREFERENCED_PARAMETER(pClip);

	if (!CGetSetOptions::GetCloudPushOnCopy())
	{
		LogMessage(_T("Clip added - push on copy disabled, skipping quick sync."));
		return;
	}

	// Trigger an immediate sync when a new clip is added
	// This ensures the new clip is pushed to the cloud quickly
	LogMessage(_T("Clip added - triggering cloud sync."));

	// SAFETY: Use a local copy of the pointer and add a guard flag
	// to prevent use-after-free if the sync manager is destroyed
	// while the fire-and-forget thread is running.
	EnterCriticalSection(&m_csSync);
	BOOL bShouldSync = (m_pSyncThread != nullptr && m_hStopEvent != nullptr);
	if (bShouldSync)
	{
		// Increment a reference-like counter to track active quick-push threads
		m_nActiveQuickSyncThreads++;
	}
	LeaveCriticalSection(&m_csSync);

	if (!bShouldSync)
	{
		OutputDebugStringA("[CloudSync] Skip quick-push: sync already stopping or not running.\n");
		return;
	}

	// Use static thread proc with context struct (AfxBeginThread doesn't support lambdas)
	QuickSyncContext* ctx = new QuickSyncContext;
	ctx->pManager = this;
	ctx->pCounter = &m_nActiveQuickSyncThreads;
	ctx->pCS = &m_csSync;

	CWinThread* pThread = AfxBeginThread(QuickSyncThreadProc, ctx, THREAD_PRIORITY_NORMAL, 0, 0);
	if (pThread)
	{
		OutputDebugStringA("[CloudSync] Spawned quick-push thread.\n");
	}
	else
	{
		delete ctx;
	}
}

UINT CCloudSyncManager::QuickSyncThreadProc(LPVOID pParam)
{
	QuickSyncContext* ctx = static_cast<QuickSyncContext*>(pParam);
	if (!ctx || !ctx->pManager)
	{
		delete ctx;
		return 1;
	}

	CCloudSyncManager* pThis = static_cast<CCloudSyncManager*>(ctx->pManager);

	// SAFETY: Check again under the lock that the manager is still alive
	EnterCriticalSection(ctx->pCS);
	BOOL bAlive = (pThis->m_pSyncThread != nullptr && pThis->m_hStopEvent != nullptr);
	if (bAlive)
	{
		// Release the lock before doing actual work (long-running)
		LeaveCriticalSection(ctx->pCS);

		// Check force upload flag (one-shot, auto-reset)
		BOOL bForce = InterlockedExchange(&pThis->m_forceOverrideRemote, 0) == 1;

		auto newGroupIds = pThis->PushGroups();
		if (!pThis->PushNewClips(bForce))
		{
			// PushNewClips failed - rollback newly created groups
			for (const auto& gid : newGroupIds)
			{
				pThis->DeleteRemoteGroup(gid);
			}
			EnterCriticalSection(&pThis->m_csStatus);
			pThis->m_csSyncStatus = _T("Error");
			pThis->m_csLastError = _T("Quick push failed");
			LeaveCriticalSection(&pThis->m_csStatus);
		}
		else
		{
			EnterCriticalSection(&pThis->m_csStatus);
			pThis->m_csSyncStatus = _T("");
			pThis->m_csLastError = _T("");
			pThis->m_lastSyncSuccessTime = time(nullptr);
			LeaveCriticalSection(&pThis->m_csStatus);
		}
	}
	else
	{
		LeaveCriticalSection(ctx->pCS);
		OutputDebugStringA("[CloudSync] Quick-push skipped: manager shutting down.\n");
	}

	// Decrement the active thread counter
	EnterCriticalSection(ctx->pCS);
	(*ctx->pCounter)--;
	LeaveCriticalSection(ctx->pCS);

	delete ctx;
	return 0;
}

void CCloudSyncManager::TriggerSync()
{
	PushGroups();
	PushNewClips();
	PullChanges();
}

void CCloudSyncManager::ForceDownloadAll()
{
	LogMessage(_T("ForceDownloadAll: starting forced download from cloud..."));

	// Track in active thread counter so Stop() waits for completion
	EnterCriticalSection(&m_csSync);
	m_nActiveQuickSyncThreads++;
	LeaveCriticalSection(&m_csSync);

	InterlockedExchange(&m_forceOverrideLocal, 1);
	AfxBeginThread(ForceSyncThreadProc, this);
}

void CCloudSyncManager::ForceUploadAll()
{
	LogMessage(_T("ForceUploadAll: starting forced upload to cloud..."));
	InterlockedExchange(&m_forceOverrideRemote, 1);
	TriggerQuickSync();
}

UINT CCloudSyncManager::ForceSyncThreadProc(LPVOID pParam)
{
	CCloudSyncManager* pThis = static_cast<CCloudSyncManager*>(pParam);
	if (!pThis)
		return 1;

	LogMessage(_T("ForceSyncThreadProc: running pull changes with override..."));
	pThis->PullGroups();
	pThis->PullChanges();
	LogMessage(_T("ForceSyncThreadProc: force download complete."));

	EnterCriticalSection(&pThis->m_csStatus);
	pThis->m_csSyncStatus = _T("");
	pThis->m_csLastError = _T("");
	pThis->m_lastSyncSuccessTime = time(nullptr);
	LeaveCriticalSection(&pThis->m_csStatus);

	// Decrement active thread counter so Stop() can complete
	EnterCriticalSection(&pThis->m_csSync);
	pThis->m_nActiveQuickSyncThreads--;
	LeaveCriticalSection(&pThis->m_csSync);

	return 0;
}

BOOL CCloudSyncManager::IsLoggedIn() const
{
	return CCloudAuth::IsLoggedIn();
}

BOOL CCloudSyncManager::IsEncryptionEnabled() const
{
	EnterCriticalSection(&m_csSync);
	BOOL bRet = m_cryptoInitialized;
	LeaveCriticalSection(&m_csSync);
	return bRet;
}

CString CCloudSyncManager::GetSyncStatus() const
{
	EnterCriticalSection(&m_csStatus);
	CString ret = m_csSyncStatus;
	LeaveCriticalSection(&m_csStatus);
	return ret;
}

CString CCloudSyncManager::GetLastError() const
{
	EnterCriticalSection(&m_csStatus);
	CString ret = m_csLastError;
	LeaveCriticalSection(&m_csStatus);
	return ret;
}

time_t CCloudSyncManager::GetLastSyncSuccessTime() const
{
	EnterCriticalSection(&m_csStatus);
	time_t ret = m_lastSyncSuccessTime;
	LeaveCriticalSection(&m_csStatus);
	return ret;
}

BOOL CCloudSyncManager::HasSyncedBefore() const
{
	EnterCriticalSection(&m_csStatus);
	BOOL ret = (m_lastSyncSuccessTime > 0);
	LeaveCriticalSection(&m_csStatus);
	return ret;
}

// ---------------------------------------------------------------------------
// InitializeEncryption
// ---------------------------------------------------------------------------
BOOL CCloudSyncManager::InitializeEncryption()
{
	try
	{
		// Read the stored AES key from registry (stored as base64)
		CString csKeyB64 = CGetSetOptions::GetCloudEncryptionKey();
		if (csKeyB64.IsEmpty())
		{
			OutputDebugString(_T("[CloudSync] No encryption key found in settings.\n"));
			return FALSE;
		}

		// Decode base64 key
		CT2A keyB64A(csKeyB64, CP_UTF8);
		std::vector<BYTE> key = CCloudCrypto::Base64Decode(CStringA(keyB64A));
		if (key.size() != 32)
		{
			OutputDebugString(_T("[CloudSync] Invalid encryption key size.\n"));
			return FALSE;
		}

		if (CCloudCrypto::Initialize(key))
		{
			EnterCriticalSection(&m_csSync);
			m_cryptoInitialized = TRUE;
			LeaveCriticalSection(&m_csSync);
			OutputDebugString(_T("[CloudSync] Encryption initialized successfully.\n"));
			return TRUE;
		}

		OutputDebugString(_T("[CloudSync] CCloudCrypto::Initialize failed.\n"));
		return FALSE;
	}
	catch (...)
	{
		OutputDebugString(_T("[CloudSync] Exception in InitializeEncryption.\n"));
		return FALSE;
	}
}

// ---------------------------------------------------------------------------
// EncryptClipFormats
// ---------------------------------------------------------------------------
BOOL CCloudSyncManager::EncryptClipFormats(nlohmann::json& formats)
{
	EnterCriticalSection(&m_csSync);
	BOOL bInitialized = m_cryptoInitialized;
	LeaveCriticalSection(&m_csSync);
	if (!bInitialized)
		return FALSE;

	try
	{
		for (auto& format : formats)
		{
			if (format.contains("data") && format["data"].is_string())
			{
				int formatType = format.value("format_type", 0);

				// CF_HDROP (format_type=15): NEVER encrypt, only sync path metadata
				// The actual file contents are NOT synced - only file paths
				if (formatType == CF_HDROP)
				{
					// Mark as file reference only, no encryption needed
					format["is_file_ref"] = true;
					continue;
				}

				std::string plainData = format["data"].get<std::string>();
				CStringA plain(plainData.c_str());
				CStringA encrypted = CCloudCrypto::Encrypt(plain);
				if (encrypted.IsEmpty())
				{
					OutputDebugStringA("[CloudSync] Failed to encrypt format data.\n");
					return FALSE;
				}
				format["data"] = encrypted.GetString();
				format["encrypted"] = true;
			}
		}
		return TRUE;
	}
	catch (...)
	{
		OutputDebugStringA("[CloudSync] Exception in EncryptClipFormats.\n");
		return FALSE;
	}
}

// ---------------------------------------------------------------------------
// DecryptClipFormats
// ---------------------------------------------------------------------------
BOOL CCloudSyncManager::DecryptClipFormats(nlohmann::json& formats)
{
	EnterCriticalSection(&m_csSync);
	BOOL bInitialized = m_cryptoInitialized;
	LeaveCriticalSection(&m_csSync);
	if (!bInitialized)
		return FALSE;

	try
	{
		for (auto& format : formats)
		{
			if (format.contains("data") && format["data"].is_string())
			{
				int formatType = format.value("format_type", 0);

				// CF_HDROP: skip decryption - file paths are stored as plain text
				if (formatType == CF_HDROP || format.value("is_file_ref", false))
				{
					continue;
				}

				// Only decrypt if marked as encrypted
				if (format.contains("encrypted") && format["encrypted"].get<bool>())
				{
					std::string encryptedData = format["data"].get<std::string>();
					CStringA encrypted(encryptedData.c_str());
					CStringA decrypted = CCloudCrypto::Decrypt(encrypted);
					if (decrypted.IsEmpty())
					{
						OutputDebugStringA("[CloudSync] Failed to decrypt format data.\n");
						return FALSE;
					}
					format["data"] = decrypted.GetString();
					format["encrypted"] = false;
				}
			}
		}
		return TRUE;
	}
	catch (...)
	{
		OutputDebugStringA("[CloudSync] Exception in DecryptClipFormats.\n");
		return FALSE;
	}
}

BOOL CCloudSyncManager::CheckAndNotifyEncryptionChange()
{
	if (!CGetSetOptions::GetCloudSyncEncryptionEnabled())
		return FALSE;

	try
	{
		EnsureHttpClient();

		auto res = m_httpClient->Get("/api/v1/encryption/salt");
		if (!res || res->status != 200)
			return FALSE;

		auto responseJson = json::parse(res->body);
		if (!responseJson.contains("data") || !responseJson["data"].contains("salt"))
			return FALSE;

		CString serverSalt(responseJson["data"]["salt"].get<std::string>().c_str());
		CString localSalt = CGetSetOptions::GetCloudEncryptionSalt();

		if (serverSalt != localSalt)
		{
			LogMessage(_T("CheckAndNotifyEncryptionChange: salt changed, notifying user."));
			CWnd* pMainWnd = AfxGetMainWnd();
			if (pMainWnd != nullptr)
			{
				::PostMessage(pMainWnd->GetSafeHwnd(), WM_CLOUD_AUTH_REQUIRED, 998, 0);
			}
			return TRUE;
		}
	}
	catch (...)
	{
		LogMessage(_T("CheckAndNotifyEncryptionChange: unexpected error"));
	}
	return FALSE;
}

UINT CCloudSyncManager::SyncThreadProc(LPVOID pParam)
{
	CCloudSyncManager* pThis = static_cast<CCloudSyncManager*>(pParam);
	if (pThis == nullptr || pThis->m_hStopEvent == nullptr)
	{
		return 1;
	}

	LogMessage(_T("Background sync thread started."));

	HANDLE waitHandles[2] = { pThis->m_hStopEvent, pThis->m_hWsTrigger };

	while (true)
	{
		// Read settings each iteration (allows live config change)
		BOOL bPeriodicSync = CGetSetOptions::GetCloudPeriodicSync();
		int nInterval = CGetSetOptions::GetCloudSyncInterval();
		DWORD dwTimeout = bPeriodicSync ? (DWORD)(nInterval * 1000) : INFINITE;

		// Wait for stop event, WS trigger, or sync interval timeout
		DWORD dwResult = WaitForMultipleObjects(2, waitHandles, FALSE, dwTimeout);
		if (dwResult == WAIT_OBJECT_0)
		{
			// Stop event was signaled
			break;
		}

		if (dwResult == WAIT_OBJECT_0 + 1)
		{
			// WS trigger was signaled — reset for next time
			ResetEvent(pThis->m_hWsTrigger);
			LogMessage(_T("Sync triggered by WebSocket event."));
		}

		// If periodic sync is off and this was a timeout (not WS trigger), skip
		if (!bPeriodicSync && dwResult == WAIT_TIMEOUT)
			continue;

		EnterCriticalSection(&pThis->m_csStatus);
		pThis->m_csSyncStatus = _T("Syncing...");
		LeaveCriticalSection(&pThis->m_csStatus);

		try
		{
			pThis->CheckAndNotifyEncryptionChange();

			// Push groups first (so group_id mappings exist before clip push)
			pThis->PushGroups();

			pThis->PushNewClips();

			// Pull groups first so group_id mappings exist before clip import
			pThis->PullGroups();
			// Then pull changes (clips can now resolve group_id to local parentId)
			pThis->PullChanges();

			EnterCriticalSection(&pThis->m_csStatus);
			pThis->m_csSyncStatus = _T("");
			pThis->m_csLastError = _T("");
			pThis->m_lastSyncSuccessTime = time(nullptr);
			LeaveCriticalSection(&pThis->m_csStatus);
		}
		catch (const std::exception& e)
		{
			CString err;
			err.Format(_T("Sync error: %hs"), e.what());
			LogMessage(err);
			EnterCriticalSection(&pThis->m_csStatus);
			pThis->m_csSyncStatus = _T("Error");
			pThis->m_csLastError = err;
			LeaveCriticalSection(&pThis->m_csStatus);
		}
		catch (...)
		{
			LogMessage(_T("Sync unknown error"));
			EnterCriticalSection(&pThis->m_csStatus);
			pThis->m_csSyncStatus = _T("Error");
			pThis->m_csLastError = _T("Unknown sync error");
			LeaveCriticalSection(&pThis->m_csStatus);
		}
	}

	LogMessage(_T("Background sync thread exiting."));
	return 0;
}

BOOL CCloudSyncManager::PushNewClips(BOOL bForce)
{
	BOOL bFirstPush = !CGetSetOptions::GetCloudInitialPushDone();

	// 原子守卫：防止并发首推（SyncThreadProc 和 QuickSyncThreadProc 同时进入）
	if (bFirstPush)
	{
		if (InterlockedExchange(&m_bFirstPushInProgress, 1) == 1)
		{
			LogMessage(_T("PushNewClips: first push already in progress, skipping concurrent attempt"));
			return TRUE;
		}
	}

	BOOL bResult = FALSE;
	try
	{
		if (bForce)
			LogMessage(_T("PushNewClips: FORCE mode - pushing ALL local clips..."));
		else
			LogMessage(_T("PushNewClips: checking for new/modified clips since last sync..."));

		time_t lastPush;
		time_t lastSync;
		EnterCriticalSection(&m_csSync);
		lastPush = m_lastPushTime;
		lastSync = m_lastSyncTime;
		LeaveCriticalSection(&m_csSync);

		int offset = bFirstPush ? (int)CGetSetOptions::GetCloudInitialPushOffset() : 0;

		time_t pushStart = time(nullptr);

		time_t baseline = 0;
		if (bFirstPush && !bForce)
		{
			baseline = GetMaxLocalClipModifiedDate();
		}

		time_t sinceTime;
		time_t upperBound;
		if (bForce)
		{
			sinceTime = 0;
			upperBound = 0;
		}
		else if (bFirstPush)
		{
			sinceTime = 0;
			upperBound = (baseline > 0) ? baseline : 0;
		}
		else
		{
			sinceTime = lastPush;
			upperBound = 0;
		}

		bool hasMore = true;
		do
		{
			json page;
			bool pageHasMore = false;
			if (!GetLocalClipsSince(sinceTime, upperBound, offset, CLOUD_PUSH_BATCH_SIZE, page, pageHasMore))
			{
				LogMessage(_T("PushNewClips: failed to enumerate local clips."));
				EnterCriticalSection(&m_csStatus);
				m_csLastError = _T("Push: failed to enumerate local clips");
				LeaveCriticalSection(&m_csStatus);
				bResult = FALSE; goto cleanup;
			}

			hasMore = pageHasMore;

			if (page.empty())
			{
				break;
			}

			json syncReq;
			if (sinceTime > 0)
			{
				SYSTEMTIME st;
				FILETIME ft;
				ULARGE_INTEGER uli;
				uli.QuadPart = ((ULONGLONG)sinceTime * 10000000ULL) + 116444736000000000ULL;
				ft.dwLowDateTime = uli.LowPart;
				ft.dwHighDateTime = uli.HighPart;
				FileTimeToSystemTime(&ft, &st);
				char timeBuf[32];
				sprintf_s(timeBuf, "%04hd-%02hd-%02hdT%02hd:%02hd:%02hdZ",
				          st.wYear, st.wMonth, st.wDay,
				          st.wHour, st.wMinute, st.wSecond);
				syncReq["since"] = std::string(timeBuf);
			}
			else
			{
				syncReq["since"] = "1970-01-01T00:00:00Z";
			}

			syncReq["device_id"] = std::string(m_deviceId);
			if (bForce)
				syncReq["force"] = true;
			syncReq["push_clips"] = page;

			EnsureHttpClient();

			std::string bodyStr = syncReq.dump();
			auto res = m_httpClient->Post("/api/v1/clips/sync", bodyStr, "application/json");
			if (!res)
			{
				LogMessage(_T("PushNewClips: failed to connect to server"));
				EnterCriticalSection(&m_csStatus);
				m_csLastError = _T("Push: failed to connect to server");
				LeaveCriticalSection(&m_csStatus);
				bResult = FALSE; goto cleanup;
			}

			if (res->status == 401 || res->status == 403)
			{
				LogMessage(_T("PushNewClips: token expired or invalid, clearing token for re-auth."));
				EnterCriticalSection(&m_csStatus);
				m_csLastError = _T("Push: authentication failed");
				LeaveCriticalSection(&m_csStatus);
				CCloudAuth::Logout();
				CWnd* pMainWnd = AfxGetMainWnd();
				if (pMainWnd != nullptr)
					::PostMessage(pMainWnd->GetSafeHwnd(), WM_CLOUD_AUTH_REQUIRED, 401, 0);
				LogMessage(_T("PushNewClips: posted WM_CLOUD_AUTH_REQUIRED message to main window"));
				bResult = FALSE; goto cleanup;
			}

			if (res->status != 200)
			{
				CString err;
				err.Format(_T("PushNewClips: server returned HTTP %d"), res->status);
				LogMessage(err);
				EnterCriticalSection(&m_csStatus);
				m_csLastError = err;
				LeaveCriticalSection(&m_csStatus);
				bResult = FALSE; goto cleanup;
			}

			try
			{
				json responseJson = json::parse(res->body);
				if (responseJson.contains("code") && responseJson["code"].get<int>() != 0)
				{
					CString msg;
					msg.Format(_T("PushNewClips: server error code %d"), responseJson["code"].get<int>());
					LogMessage(msg);
					EnterCriticalSection(&m_csStatus);
					m_csLastError = msg;
					LeaveCriticalSection(&m_csStatus);
					bResult = FALSE; goto cleanup;
				}
				const json* dataNode = nullptr;
				if (responseJson.contains("data") && responseJson["data"].is_object())
					dataNode = &responseJson["data"];
				else
					dataNode = &responseJson;
				int syncedCount = dataNode->value("updated_count", 0);
				int skippedCount = dataNode->value("skipped_count", 0);
				CString msg;
				msg.Format(_T("PushNewClips: %d clips synced, %d skipped (CRC duplicates)"), syncedCount, skippedCount);
				LogMessage(msg);
			}
			catch (const json::parse_error& e)
			{
				CString err;
				err.Format(_T("PushNewClips: JSON parse error: %hs"), e.what());
				LogMessage(err);
				EnterCriticalSection(&m_csStatus);
				m_csLastError = err;
				LeaveCriticalSection(&m_csStatus);
				bResult = FALSE; goto cleanup;
			}

			offset += (int)page.size();
			if (bFirstPush)
				CGetSetOptions::SetCloudInitialPushOffset(offset);

		} while (hasMore);

		EnterCriticalSection(&m_csSync);
		if (bFirstPush)
		{
			m_lastPushTime = (baseline > 0) ? baseline : pushStart;
		}
		else
		{
			m_lastPushTime = pushStart;
		}
		CGetSetOptions::SetCloudLastPushTime((__int64)m_lastPushTime);
		LeaveCriticalSection(&m_csSync);

		if (bFirstPush && !hasMore)
		{
			CGetSetOptions::SetCloudInitialPushDone(TRUE);
			CGetSetOptions::SetCloudInitialPushOffset(0);
		}

		bResult = TRUE; goto cleanup;
	}
	catch (const std::exception& e)
	{
		CString err;
		err.Format(_T("PushNewClips error: %hs"), e.what());
		LogMessage(err);
		EnterCriticalSection(&m_csStatus);
		m_csLastError = err;
		LeaveCriticalSection(&m_csStatus);
		bResult = FALSE;
	}
	catch (...)
	{
		LogMessage(_T("PushNewClips: unknown error"));
		EnterCriticalSection(&m_csStatus);
		m_csLastError = _T("Push: unknown error");
		LeaveCriticalSection(&m_csStatus);
		bResult = FALSE;
	}

cleanup:
	if (bFirstPush)
		InterlockedExchange(&m_bFirstPushInProgress, 0);
	return bResult;
}

// ---------------------------------------------------------------------------
// ExtractFilePathsFromHDROP: extract file paths from CF_HDROP format data
// The CF_HDROP data contains null-separated file paths, double-null terminated
// ---------------------------------------------------------------------------
nlohmann::json CCloudSyncManager::ExtractFilePathsFromHDROP(const nlohmann::json& hdropFormat)
{
	nlohmann::json paths = nlohmann::json::array();

	if (!hdropFormat.contains("data") || !hdropFormat["data"].is_string())
	{
		return paths;
	}

	try
	{
		std::string dataStr = hdropFormat["data"].get<std::string>();
		// CF_HDROP data is typically base64-encoded binary DROPFILES structure + file paths
		// For cloud sync, we only care about the file path strings
		// In the simplest case, the data might already be a text representation of paths

		// Parse the file paths (assuming newline or null separated)
		CStringA data(dataStr.c_str());
		CStringA remaining = data;
		CStringA path;

		while (!remaining.IsEmpty())
		{
			// Find null or newline separator
			int pos = -1;
			for (int i = 0; i < remaining.GetLength(); i++)
			{
				char ch = remaining[i];
				if (ch == '\0' || ch == '\n' || ch == '\r')
				{
					if (i > 0) // skip leading whitespace chars
					{
						pos = i;
						break;
					}
					remaining = remaining.Mid(i + 1);
					continue;
				}
			}

			if (pos > 0)
			{
				path = remaining.Left(pos);
				remaining = remaining.Mid(pos + 1);

				// Skip empty paths
				if (!path.IsEmpty() && path.GetLength() > 1)
				{
					paths.push_back(path.GetString());
				}
			}
			else
			{
				// Last path
				if (!remaining.IsEmpty() && remaining.GetLength() > 1)
				{
					paths.push_back(remaining.GetString());
				}
				break;
			}
		}
	}
	catch (...)
	{
		LogMessage(_T("ExtractFilePathsFromHDROP: unexpected error"));
	}
	return paths;
}

// ---------------------------------------------------------------------------
// FilterHDROPForSync: replace CF_HDROP format with path-only metadata
// This ensures file contents are NEVER synced, only file paths
// ---------------------------------------------------------------------------
BOOL CCloudSyncManager::FilterHDROPForSync(nlohmann::json& formats)
{
	BOOL bFound = FALSE;
	try
	{
		for (auto& format : formats)
		{
			int formatType = format.value("format_type", 0);
			if (formatType == CF_HDROP)
			{
				// Extract file paths
				nlohmann::json filePaths = ExtractFilePathsFromHDROP(format);

				// Replace the format data with path metadata only
				nlohmann::json pathMeta;
				pathMeta["type"] = "file_paths";
				pathMeta["paths"] = filePaths;
				pathMeta["count"] = filePaths.size();

				// Store as JSON string instead of binary data
				format["data"] = pathMeta.dump();
				format["is_file_ref"] = true;
				format["encrypted"] = false; // Never encrypt file paths

				CString msg;
				msg.Format(_T("CF_HDROP filtered: %d file paths (contents NOT synced)"), (int)filePaths.size());
				LogMessage(msg);

				bFound = TRUE;
				break;
			}
		}
	}
	catch (const std::exception& e)
	{
		CString err;
		err.Format(_T("FilterHDROPForSync error: %hs"), e.what());
		LogMessage(err);
	}
	catch (...)
	{
		LogMessage(_T("FilterHDROPForSync: unknown error"));
	}

	return bFound;
}

void CCloudSyncManager::PullChanges()
{
	try
	{
		LogMessage(_T("PullChanges: checking for changes from other devices..."));

		// Build since timestamp as RFC3339 (thread-safe read)
		time_t lastSync;
		EnterCriticalSection(&m_csSync);
		lastSync = m_lastSyncTime;
		LeaveCriticalSection(&m_csSync);

		CStringA sinceStr;
		if (lastSync > 0)
		{
			CTime sinceTime((time_t)lastSync);
			sinceStr.Format("%04hd-%02hd-%02hdT%02hd:%02hd:%02hdZ",
				sinceTime.GetYear(), sinceTime.GetMonth(), sinceTime.GetDay(),
				sinceTime.GetHour(), sinceTime.GetMinute(), sinceTime.GetSecond());
		}
		else
		{
			sinceStr = "1970-01-01T00:00:00Z";
		}

		// Use GET /clips/changes for pull-only (server has a dedicated pull endpoint)
		EnsureHttpClient();

		// GET /api/v1/clips/changes?since=...
		CStringA path;
		path.Format("/api/v1/clips/changes?since=%s", (LPCSTR)sinceStr);
		auto res = m_httpClient->Get(path.GetString());
		if (!res)
		{
			LogMessage(_T("PullChanges: failed to connect to server"));
			EnterCriticalSection(&m_csStatus);
			m_csLastError = _T("Pull: failed to connect to server");
			LeaveCriticalSection(&m_csStatus);
			return;
		}

		// Handle authentication errors (401/403) - trigger re-auth flow
		if (res->status == 401 || res->status == 403)
		{
			LogMessage(_T("PullChanges: token expired or invalid, clearing token for re-auth."));
			EnterCriticalSection(&m_csStatus);
			m_csLastError = _T("Pull: authentication failed");
			LeaveCriticalSection(&m_csStatus);
			
			// Clear stored credentials
			CCloudAuth::Logout();
			
			// Notify user via main window (post message to avoid blocking sync thread)
			CWnd* pMainWnd = AfxGetMainWnd();
			if (pMainWnd != nullptr)
			{
				// Post custom message with 401 status code
				::PostMessage(pMainWnd->GetSafeHwnd(), WM_CLOUD_AUTH_REQUIRED, 401, 0);
			}
			
			LogMessage(_T("PullChanges: posted WM_CLOUD_AUTH_REQUIRED message to main window"));
			return;
		}

		if (res->status != 200)
		{
			CString err;
			err.Format(_T("PullChanges: server returned HTTP %d"), res->status);
			LogMessage(err);
			EnterCriticalSection(&m_csStatus);
			m_csLastError = err;
			LeaveCriticalSection(&m_csStatus);
			return;
		}

		// Parse response
		try
		{
			json responseJson = json::parse(res->body);

			// Server wraps response in { code: 0, data: { clips, server_time, has_more, deleted_ids } }
			if (responseJson.contains("code") && responseJson["code"].get<int>() != 0)
			{
				CString msg;
				msg.Format(_T("PullChanges: server error code %d"), responseJson["code"].get<int>());
				LogMessage(msg);
				EnterCriticalSection(&m_csStatus);
				m_csLastError = msg;
				LeaveCriticalSection(&m_csStatus);
				return;
			}

			// Extract from data envelope
			const json* dataNode = nullptr;
			if (responseJson.contains("data") && responseJson["data"].is_object())
			{
				dataNode = &responseJson["data"];
			}
			else
			{
				dataNode = &responseJson; // Fallback
			}

			// Check for new clips (server uses "clips" for GET /changes endpoint)
			const json* clipsNode = nullptr;
			if (dataNode->contains("new_clips"))
			{
				clipsNode = &(*dataNode)["new_clips"];  // POST /sync response
			}
			else if (dataNode->contains("clips"))
			{
				clipsNode = &(*dataNode)["clips"];      // GET /changes response
			}

			const json* deletedNode = nullptr;
			if (dataNode->contains("deleted_ids"))
			{
				deletedNode = &(*dataNode)["deleted_ids"];
			}

			bool hasClips = (clipsNode && !clipsNode->empty());
			bool hasDeletions = (deletedNode && !deletedNode->empty());

			if (!hasClips && !hasDeletions)
			{
				LogMessage(_T("PullChanges: no new clips or deletions from other devices"));
				return;
			}

			// Process each new clip
			int mergedCount = 0;
			BOOL bForce = FALSE;
			if (hasClips)
			{
				// Read force-override-local flag once for all clips in this pull cycle
				bForce = InterlockedExchange(&m_forceOverrideLocal, 0) == 1;

				for (const auto& clip : *clipsNode)
				{
					// Decrypt formats if encryption is enabled
					json formats = clip.contains("formats") ? clip["formats"] : json::array();
					EnterCriticalSection(&m_csSync);
					BOOL bCryptoInit = m_cryptoInitialized;
					LeaveCriticalSection(&m_csSync);
					if (bCryptoInit && !formats.empty())
					{
						if (!DecryptClipFormats(formats))
						{
							CString msg;
							msg.Format(_T("PullChanges: decryption failed for clip, skipping"));
							LogMessage(msg);
							continue;
						}
					}

					// Merge clip into local database with LWW conflict resolution
					int newId = MergeRemoteClipToLocal(clip, bForce);
					if (newId > 0)
					{
						mergedCount++;
					}
				}
			}

			// Process deleted clips (M1: use mapping table for string IDs)
			int deletedCount = 0;
			if (hasDeletions)
			{
				for (const auto& deletedIdVal : *deletedNode)
				{
					std::string idStr = deletedIdVal.get<std::string>();

					// Look up local ID via mapping table (supports string UUIDs)
					int localId = GetLocalIdByRemoteId(idStr);

					if (localId > 0)
					{
						if (DeleteLocalClip(localId))
						{
							deletedCount++;
							CString msg;
							msg.Format(_T("PullChanges: deleted local clip %d (remote %hs)"), localId, idStr.c_str());
							LogMessage(msg);
						}
					}
					else
					{
						CString msg;
						msg.Format(_T("PullChanges: clip %hs not found in mapping table, skip delete"), idStr.c_str());
						LogMessage(msg);
					}
				}
			}

			// Process dont_sync_ids: mark locally but keep content
			if (dataNode->contains("dont_sync_ids") && !(*dataNode)["dont_sync_ids"].is_null())
			{
				auto& dontSyncIds = (*dataNode)["dont_sync_ids"];
				for (const auto& idVal : dontSyncIds)
				{
					std::string idStr = idVal.get<std::string>();
					int localId = GetLocalIdByRemoteId(idStr);
					if (localId > 0)
					{
						CSingleLock lockDb(&m_csDb, TRUE);
						CString csSQL;
						csSQL.Format(_T("UPDATE Main SET lDontSync = 1 WHERE lID = %d"), localId);
						theApp.m_db.execDML(csSQL);
						CString msg;
						msg.Format(_T("PullChanges: marked clip as dont_sync (local %d, remote %hs)"), localId, idStr.c_str());
						LogMessage(msg);
					}
				}
			}

			// Update last sync time from server's sync_time (or use current time as fallback)
			time_t newSyncTime = time(nullptr);
			if (dataNode->contains("server_time"))
			{
				std::string serverTimeStr = (*dataNode)["server_time"].get<std::string>();
				SYSTEMTIME st = {};
				if (sscanf_s(serverTimeStr.c_str(), "%04hd-%02hd-%02hdT%02hd:%02hd:%02hdZ",
					&st.wYear, &st.wMonth, &st.wDay,
					&st.wHour, &st.wMinute, &st.wSecond) == 6)
				{
					FILETIME ft;
					SystemTimeToFileTime(&st, &ft);
					ULARGE_INTEGER uli;
					uli.LowPart = ft.dwLowDateTime;
					uli.HighPart = ft.dwHighDateTime;
					newSyncTime = static_cast<time_t>((uli.QuadPart - 116444736000000000ULL) / 10000000ULL);
				}
			}
			else if (dataNode->contains("sync_time"))
			{
				std::string syncTimeStr = (*dataNode)["sync_time"].get<std::string>();
				SYSTEMTIME st = {};
				if (sscanf_s(syncTimeStr.c_str(), "%04hd-%02hd-%02hdT%02hd:%02hd:%02hdZ",
					&st.wYear, &st.wMonth, &st.wDay,
					&st.wHour, &st.wMinute, &st.wSecond) == 6)
				{
					FILETIME ft;
					SystemTimeToFileTime(&st, &ft);
					ULARGE_INTEGER uli;
					uli.LowPart = ft.dwLowDateTime;
					uli.HighPart = ft.dwHighDateTime;
					newSyncTime = static_cast<time_t>((uli.QuadPart - 116444736000000000ULL) / 10000000ULL);
				}
			}

			EnterCriticalSection(&m_csSync);
			m_lastSyncTime = newSyncTime;
			LeaveCriticalSection(&m_csSync);
			CGetSetOptions::SetCloudLastSyncTime((__int64)newSyncTime);

			CString msg;
			if (deletedCount > 0)
			{
				msg.Format(_T("PullChanges: received %d clips (%d merged), %d deletions"),
					hasClips ? clipsNode->size() : 0, mergedCount, deletedCount);
			}
			else
			{
				msg.Format(_T("PullChanges: received %d clips, %d merged to local DB"),
					hasClips ? clipsNode->size() : 0, mergedCount);
			}
			LogMessage(msg);
		}
		catch (const json::parse_error& e)
		{
			CString err;
			err.Format(_T("PullChanges: JSON parse error: %hs"), e.what());
			LogMessage(err);
			EnterCriticalSection(&m_csStatus);
			m_csLastError = err;
			LeaveCriticalSection(&m_csStatus);
		}
	}
	catch (const std::exception& e)
	{
		CString err;
		err.Format(_T("PullChanges error: %hs"), e.what());
		LogMessage(err);
		EnterCriticalSection(&m_csStatus);
		m_csLastError = err;
		LeaveCriticalSection(&m_csStatus);
	}
	catch (...)
	{
		LogMessage(_T("PullChanges: unknown error"));
		EnterCriticalSection(&m_csStatus);
		m_csLastError = _T("Pull: unknown error");
		LeaveCriticalSection(&m_csStatus);
	}
}

// ---------------------------------------------------------------------------
// GetMaxLocalClipModifiedDate: Get the maximum lModifiedDate from Main table
// Used for first-push baseline snapshot to avoid OFFSET skip due to inserts.
// ---------------------------------------------------------------------------
time_t CCloudSyncManager::GetMaxLocalClipModifiedDate() const
{
	CSingleLock lockDb(&m_csDb, TRUE);
	CString csSQL = _T("SELECT MAX(lModifiedDate) FROM Main WHERE bIsGroup = 0 AND lDontSync = 0");
	CppSQLite3Query q = theApp.m_db.execQuery(csSQL);
	if (q.eof() == false)
	{
		return (time_t)q.getInt64Field(0);
	}
	return 0;
}

// ---------------------------------------------------------------------------
// GetLocalClipsSince: Enumerate local clips modified since lastSyncTime
// Produces JSON matching server's PushClipItem schema:
//   { id, description, crc, group_id, short_cut, updated_at, formats: [{format_type, data}] }
// ---------------------------------------------------------------------------
BOOL CCloudSyncManager::GetLocalClipsSince(time_t sinceTime, time_t upperBound, int offset, int limit, nlohmann::json& clipsArray, bool& hasMore)
{
	clipsArray = nlohmann::json::array();
	hasMore = false;

	try
	{
		CString where;
		if (sinceTime > 0)
		{
			CString cond;
			cond.Format(_T("lModifiedDate > %lld"), sinceTime);
			where = cond;
		}
		if (upperBound > 0)
		{
			CString cond;
			cond.Format(_T("lModifiedDate <= %lld"), upperBound);
			if (!where.IsEmpty()) where += _T(" AND ");
			where += cond;
		}
		if (!where.IsEmpty())
		{
			CString tmp;
			tmp.Format(_T("WHERE %s AND "), (LPCTSTR)where);
			where = tmp;
		}
		else
		{
			where = _T("WHERE ");
		}

		CString csSQL;
		csSQL.Format(_T("SELECT lID, lDate, mText, CRC, bIsGroup, lParentID, ")
		             _T("clipOrder, clipGroupOrder, stickyClipOrder, lShortCut, globalShortCut, ")
		             _T("lDontAutoDelete, lDontSync, m_Description, lastPasteDate, lModifiedDate ")
		             _T("FROM Main %sbIsGroup = 0 AND lDontSync = 0 ")
		             _T("ORDER BY lModifiedDate DESC LIMIT %d OFFSET %d"),
		             (LPCTSTR)where, limit, offset);

		CSingleLock lockDb(&m_csDb, TRUE);
		CppSQLite3Query q = theApp.m_db.execQuery(csSQL);

		int pageCount = 0;
		while (q.eof() == false)
		{
			int clipId = q.getIntField(_T("lID"));
			time_t lDate = (time_t)q.getInt64Field(_T("lDate"));
			CString desc = q.getStringField(_T("mText"));
			DWORD crc = (DWORD)q.getIntField(_T("CRC"));
			time_t modDate = (time_t)q.getInt64Field(_T("lModifiedDate"));

			json clipJson;

			// Use existing remote ID mapping if available, otherwise generate a UUID
			// to avoid cross-device ID collisions (local auto-increment IDs are not globally unique)
			std::string remoteClipId = GetRemoteIdByLocalId(clipId);
			if (remoteClipId.empty())
			{
				remoteClipId = CStringToStdString(NewGuidString());
			}
			clipJson["id"] = remoteClipId;
			SaveRemoteIdMapping(clipId, remoteClipId);
			clipJson["description"] = CStringToStdString(desc);
			clipJson["crc"] = static_cast<int64_t>(crc);
			clipJson["group_id"] = "";
			int localParentId = q.getIntField(_T("lParentID"));
			if (localParentId > 0)
			{
				std::string remoteGroupId = GetRemoteGroupIdByLocalId(localParentId);
				if (!remoteGroupId.empty())
					clipJson["group_id"] = remoteGroupId;
			}
			clipJson["short_cut"] = q.getIntField(_T("lShortCut"));
			clipJson["clip_order"] = q.getFloatField(_T("clipOrder"));
			clipJson["clip_group_order"] = q.getFloatField(_T("clipGroupOrder"));

			time_t updatedAt = (modDate > 0) ? modDate : lDate;
			if (updatedAt > 0)
			{
				struct tm gmtm;
				gmtime_s(&gmtm, &updatedAt);
				char timeBuf[32];
				strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%SZ", &gmtm);
				clipJson["updated_at"] = std::string(timeBuf);
			}

			json formatsArray;
			if (LoadClipFormats(clipId, formatsArray))
			{
				FilterHDROPForSync(formatsArray);

				json serverFormats;
				for (const auto& fmt : formatsArray)
				{
					json serverFmt;
					serverFmt["format_type"] = fmt.value("format_type", 0);
					serverFmt["data"] = fmt.value("data", "");
					serverFormats.push_back(serverFmt);
				}
				clipJson["formats"] = serverFormats;

				EnterCriticalSection(&m_csSync);
				BOOL bCryptoInit = m_cryptoInitialized;
				LeaveCriticalSection(&m_csSync);
				if (bCryptoInit && !serverFormats.empty())
				{
					if (!EncryptClipFormats(clipJson["formats"]))
					{
						CString msg;
						msg.Format(_T("GetLocalClipsSince: encryption failed for clip %d, skipping"), clipId);
						LogMessage(msg);
						q.nextRow();
						continue;
					}
				}
			}
			else
			{
				clipJson["formats"] = json::array();
			}

			clipsArray.push_back(clipJson);
			pageCount++;

			q.nextRow();
		}
		lockDb.Unlock();

		hasMore = (pageCount >= limit);

		CString msg;
		msg.Format(_T("GetLocalClipsSince: page offset=%d, got %d clips"), offset, pageCount);
		LogMessage(msg);

		return TRUE;
	}
	catch (const CppSQLite3Exception& e)
	{
		CString err;
		err.Format(_T("GetLocalClipsSince SQLite error: %hs"), e.errorMessage());
		LogMessage(err);
		return FALSE;
	}
	catch (const std::exception& e)
	{
		CString err;
		err.Format(_T("GetLocalClipsSince error: %hs"), e.what());
		LogMessage(err);
		return FALSE;
	}
	catch (...)
	{
		LogMessage(_T("GetLocalClipsSince: unknown error"));
		return FALSE;
	}
}

// ---------------------------------------------------------------------------
// LoadClipFormats: Load all format data for a clip from the Data table
// ---------------------------------------------------------------------------
BOOL CCloudSyncManager::LoadClipFormats(int clipId, nlohmann::json& formatsArray)
{
	formatsArray = nlohmann::json::array();

	try
	{
		CString csSQL;
		csSQL.Format(_T("SELECT lID, strClipBoardFormat, ooData FROM Data ")
		             _T("WHERE lParentID = %d ORDER BY lID"), clipId);

		CSingleLock lockDb(&m_csDb, TRUE);
		CppSQLite3Query q = theApp.m_db.execQuery(csSQL);

		while (q.eof() == false)
		{
			int formatId = q.getIntField(_T("lID"));
			CString formatName = q.getStringField(_T("strClipBoardFormat"));

			// Get format type ID
			UINT cfType = GetFormatID(formatName);

			// Get blob data
			int nDataLen = 0;
			const unsigned char* cData = q.getBlobField(_T("ooData"), nDataLen);

			json formatJson;
			formatJson["format_id"] = formatId;
			formatJson["format_name"] = CStringToStdString(formatName);
			formatJson["format_type"] = (int)cfType;

			if (cData != nullptr && nDataLen > 0)
			{
				// For text formats, store as plain text
				if (cfType == CF_TEXT || cfType == CF_UNICODETEXT)
				{
					if (cfType == CF_UNICODETEXT)
					{
						// Convert UTF-16 to UTF-8
						CStringW wideData((LPCWSTR)cData, nDataLen / 2);
						CT2A utf8Data(wideData, CP_UTF8);
						formatJson["data"] = std::string(utf8Data.m_psz);
					}
					else
					{
						formatJson["data"] = std::string(reinterpret_cast<const char*>(cData), nDataLen);
					}
				}
				else
				{
					// For binary formats (images, files, etc.), use base64 encoding
					// This ensures Web frontend can correctly decode and display images (CF_DIB)
					std::vector<BYTE> binaryData(cData, cData + nDataLen);
					CStringA base64Data = CCloudCrypto::Base64Encode(binaryData);
					formatJson["data"] = std::string(base64Data.GetString());
					formatJson["encoding"] = "base64";
				}

				formatJson["data_size"] = nDataLen;
			}
			else
			{
				formatJson["data"] = "";
				formatJson["data_size"] = 0;
			}

			formatsArray.push_back(formatJson);
			q.nextRow();
		}

		return formatsArray.size() > 0;
	}
	catch (const CppSQLite3Exception& e)
	{
		CString err;
		err.Format(_T("LoadClipFormats SQLite error: %hs"), e.errorMessage());
		LogMessage(err);
		return FALSE;
	}
	catch (const std::exception& e)
	{
		CString err;
		err.Format(_T("LoadClipFormats error: %hs"), e.what());
		LogMessage(err);
		return FALSE;
	}
	catch (...)
	{
		LogMessage(_T("LoadClipFormats: unknown error"));
		return FALSE;
	}
}

// ---------------------------------------------------------------------------
// MergeRemoteClipToLocal: Merge a remote clip into local database with LWW
// Server sends: { id, description, crc, created_at, updated_at, group_id,
//                 short_cut, paste_count, formats: [{format_type, data, data_size}] }
// LWW: If local clip has newer updated_at, skip. If remote is newer, update.
// Returns the new/updated clip ID, or -1 on error
// ---------------------------------------------------------------------------
int CCloudSyncManager::MergeRemoteClipToLocal(const nlohmann::json& remoteClip, BOOL bForce)
{
	try
	{
		// Extract clip metadata from server's ClipDetail schema
		std::string serverIdStr = remoteClip.value("id", "");
		std::string desc = remoteClip.value("description", "");
		int64_t crc64 = remoteClip.value("crc", (int64_t)0);
		DWORD crc = static_cast<DWORD>(crc64);
		int shortcut = remoteClip.value("short_cut", 0);

		// Parse remote updated_at timestamp for LWW comparison
		time_t remoteUpdatedAt = 0;
		if (remoteClip.contains("updated_at"))
		{
			std::string updatedAtStr = remoteClip["updated_at"].get<std::string>();
			SYSTEMTIME st = {};
			if (sscanf_s(updatedAtStr.c_str(), "%04hd-%02hd-%02hdT%02hd:%02hd:%02hdZ",
				&st.wYear, &st.wMonth, &st.wDay,
				&st.wHour, &st.wMinute, &st.wSecond) == 6)
			{
				FILETIME ft;
				if (SystemTimeToFileTime(&st, &ft))
				{
					ULARGE_INTEGER uli;
					uli.LowPart = ft.dwLowDateTime;
					uli.HighPart = ft.dwHighDateTime;
					remoteUpdatedAt = static_cast<time_t>((uli.QuadPart - 116444736000000000ULL) / 10000000ULL);
				}
			}
		}

		// Check if clip already exists locally (by CRC match - same content)
		int existingId = -1;
		time_t localModDate = 0;
		try
		{
			// Primary check: CRC match (same content already exists)
			// Use parameterized query to prevent SQL injection
			CSingleLock lockDb(&m_csDb, TRUE);
			CString csSQL;
			csSQL.Format(_T("SELECT lID, lModifiedDate, mText FROM Main WHERE CRC = ? AND bIsGroup = 0 LIMIT 1"));
			
			CppSQLite3Statement stmt = theApp.m_db.compileStatement(csSQL);
			stmt.bind(1, (int)crc);
			CppSQLite3Query q = stmt.execQuery();
			
			if (q.eof() == false)
			{
				existingId = q.getIntField(_T("lID"));
				localModDate = (time_t)q.getInt64Field(_T("lModifiedDate"));
				CString localText = q.getStringField(_T("mText"), _T(""));
				CString remoteDesc(CA2W(desc.c_str(), CP_UTF8));
				// Compare full content to detect CRC-32 collisions
				if (localText != remoteDesc)
				{
					LogMessage(_T("MergeRemoteClipToLocal: CRC collision detected (mText differs), treating as new clip"));
					existingId = -1;
				}
			}
			lockDb.Unlock();
		}
		catch (...)
		{
			LogMessage(_T("MergeRemoteClipToLocal: CRC query failed, will create new clip"));
		}

		if (existingId > 0)
		{
			// Clip with same content (CRC) already exists locally
			if (bForce)
			{
				// Force mode: unconditionally replace local with remote
				int deletedId = existingId;
				DeleteLocalClip(existingId);
				existingId = -1;
				CString msg;
				msg.Format(_T("MergeRemoteClipToLocal: force override, deleting clip %d to replace with remote"), deletedId);
				LogMessage(msg);
				// Do NOT return - fall through to create new clip with remote content
			}
			else if (remoteUpdatedAt > localModDate)
			{
				// Remote clip is newer - update local clip's modification time
				// (Content is same per CRC match, so no need to update formats)
				CSingleLock lockDb(&m_csDb, TRUE);
				CString csUpdateSQL;
				csUpdateSQL.Format(_T("UPDATE Main SET lModifiedDate = %lld WHERE lID = %d"),
				                   (__int64)remoteUpdatedAt, existingId);
				theApp.m_db.execDML(csUpdateSQL);

				CString msg;
				msg.Format(_T("MergeRemoteClipToLocal: clip %d exists, remote newer (CRC match), updated timestamp"), existingId);
				LogMessage(msg);
				SaveRemoteIdMapping(existingId, serverIdStr);
				return existingId;
			}
			else
			{
				// Local clip is same age or newer - skip (LWW: local wins)
				CString msg;
				msg.Format(_T("MergeRemoteClipToLocal: duplicate clip (CRC=%d, local=%lld, remote=%lld), skipping (LWW: local wins)"),
				           crc, (long long)localModDate, (long long)remoteUpdatedAt);
				LogMessage(msg);
				SaveRemoteIdMapping(existingId, serverIdStr);
				return existingId;
			}
		}

		// No CRC match - check if description matches (fallback for clips without CRC)
		if (!desc.empty())
		{
			try
			{
				// Use parameterized query to prevent SQL injection
				CSingleLock lockDb(&m_csDb, TRUE);
				CString csSQL;
				csSQL.Format(_T("SELECT lID, lModifiedDate, CRC FROM Main WHERE mText = ? AND bIsGroup = 0 LIMIT 1"));
				
				CppSQLite3Statement stmt = theApp.m_db.compileStatement(csSQL);
				stmt.bind(1, CString(desc.c_str()));
				CppSQLite3Query q = stmt.execQuery();
				
				if (q.eof() == false)
				{
					int descMatchId = q.getIntField(_T("lID"));
					localModDate = (time_t)q.getInt64Field(_T("lModifiedDate"));
					DWORD localCRC = (DWORD)q.getIntField(_T("CRC"));

					if (bForce)
					{
						// Force mode: unconditionally replace this clip
						existingId = descMatchId;
					}
					// Same description but different CRC -> different content
					// LWW: only skip if local is same age or newer
					else if (remoteUpdatedAt <= localModDate)
					{
						time_t timeDiff = localModDate - remoteUpdatedAt;
						if (timeDiff <= 1 && localCRC != crc)
						{
							// Conflict: both changed within 1s with different content
							// Save remote as a new clip (don't replace local)
							CString msg;
							msg.Format(_T("MergeRemoteClipToLocal: CONFLICT (same desc, different CRC, local=%lld, remote=%lld, diff=%llds), saving remote as new clip"),
							           (long long)localModDate, (long long)remoteUpdatedAt, (long long)timeDiff);
							LogMessage(msg);
						}
						else
						{
							CString msg;
							msg.Format(_T("MergeRemoteClipToLocal: same description, different CRC, local is newer (local=%lld, remote=%lld), skipping"),
							           (long long)localModDate, (long long)remoteUpdatedAt);
							LogMessage(msg);
							SaveRemoteIdMapping(descMatchId, serverIdStr);
							return descMatchId;
						}
					}
					else
					{
						// Remote is newer - will replace this clip's content
						existingId = descMatchId;
					}
				}
			}
			catch (...)
			{
				LogMessage(_T("MergeRemoteClipToLocal: description match query failed, will create new clip"));
			}
		}

		// Delete existing clip if we found one that needs to be replaced
		if (existingId > 0)
		{
			DeleteLocalClip(existingId);
			existingId = -1;  // Will create fresh clip below
		}

		// Create new clip in local database
		CClip newClip;
		newClip.m_Desc = CString(desc.c_str());
		newClip.m_CRC = crc;
		newClip.m_parentId = -1;  // Top-level clip
		// Handle group_id from remote
		if (remoteClip.contains("group_id") && !remoteClip["group_id"].is_null())
		{
			std::string remoteGroupId = remoteClip["group_id"].get<std::string>();
			if (!remoteGroupId.empty())
			{
				int localGroupId = GetLocalGroupIdByRemoteId(remoteGroupId);
				if (localGroupId > 0)
					newClip.m_parentId = localGroupId;
			}
		}
		newClip.m_bIsGroup = FALSE;

		// Use remote's updated_at as the clip timestamp (preserves ordering)
		if (remoteUpdatedAt > 0)
		{
			newClip.m_Time = CTime((time_t)remoteUpdatedAt);
		}
		else
		{
			newClip.m_Time = CTime::GetCurrentTime();
		}

		// Handle clip_order and clip_group_order from remote
		if (remoteClip.contains("clip_order"))
			newClip.m_clipOrder = remoteClip["clip_order"].get<double>();
		if (remoteClip.contains("clip_group_order"))
			newClip.m_clipGroupOrder = remoteClip["clip_group_order"].get<double>();

		// Load formats from server response
		if (remoteClip.contains("formats") && remoteClip["formats"].is_array())
		{
			for (const auto& formatJson : remoteClip["formats"])
			{
				std::string dataStr = formatJson.value("data", "");
				int dataSize = formatJson.value("data_size", 0);
				int formatType = formatJson.value("format_type", 0);

				if (dataStr.empty())
					continue;

				// Decode data based on whether it's base64 or plain text
				HGLOBAL hGlobal = nullptr;

				// Determine if data is base64-encoded or plain text
				// Base64 data is typically longer than the decoded size, contains only [A-Za-z0-9+/=]
				bool isBase64 = (dataSize > 0 && dataStr.length() > static_cast<size_t>(dataSize));

				if (formatType == CF_UNICODETEXT)
				{
					CStringA utf8Data(dataStr.c_str());
					int wideLen = MultiByteToWideChar(CP_UTF8, 0,
						utf8Data.GetString(), utf8Data.GetLength(), nullptr, 0);
					if (wideLen > 0)
					{
						hGlobal = GlobalAlloc(GMEM_MOVEABLE, (wideLen + 1) * sizeof(wchar_t));
						if (hGlobal)
						{
							wchar_t* pData = (wchar_t*)GlobalLock(hGlobal);
							MultiByteToWideChar(CP_UTF8, 0,
								utf8Data.GetString(), utf8Data.GetLength(),
								pData, wideLen);
							pData[wideLen] = L'\0';
							GlobalUnlock(hGlobal);
						}
					}
				}
				else if (formatType == CF_TEXT)
				{
					// Plain text
					hGlobal = GlobalAlloc(GMEM_MOVEABLE, dataStr.length() + 1);
					if (hGlobal)
					{
						char* pData = (char*)GlobalLock(hGlobal);
						memcpy(pData, dataStr.c_str(), dataStr.length());
						pData[dataStr.length()] = '\0';
						GlobalUnlock(hGlobal);
					}
				}
				else if (isBase64)
				{
					// Base64-encoded binary data (CF_DIB images, etc.)
					CStringA b64Str(dataStr.c_str());
					std::vector<BYTE> decoded = CCloudCrypto::Base64Decode(b64Str);
					if (!decoded.empty())
					{
						hGlobal = GlobalAlloc(GMEM_MOVEABLE, decoded.size());
						if (hGlobal)
						{
							BYTE* pData = (BYTE*)GlobalLock(hGlobal);
							memcpy(pData, decoded.data(), decoded.size());
							GlobalUnlock(hGlobal);
						}
					}
				}
				else
				{
					// Plain text fallback
					hGlobal = GlobalAlloc(GMEM_MOVEABLE, dataStr.length() + 1);
					if (hGlobal)
					{
						char* pData = (char*)GlobalLock(hGlobal);
						memcpy(pData, dataStr.c_str(), dataStr.length());
						pData[dataStr.length()] = '\0';
						GlobalUnlock(hGlobal);
					}
				}

				if (hGlobal)
				{
					// Map server format_type to Windows clipboard format
					UINT cfType = 0;
					switch (formatType)
					{
						case 1:  cfType = CF_TEXT; break;
						case 13: cfType = CF_UNICODETEXT; break;
						case 8:  cfType = RegisterClipboardFormat(_T("CF_DIB")); break;
						case 49: cfType = RegisterClipboardFormat(_T("HTML Format")); break;
						case 15: cfType = CF_HDROP; break;
						default: cfType = formatType; break;
					}

					CClipFormat cf;
					cf.m_cfType = cfType;
					cf.m_hgData = hGlobal;
					cf.m_dataId = -1;
					cf.m_parentId = -1;
					newClip.m_Formats.Add(cf);
				}
			}
		}

		// Save to database
		if (newClip.m_Formats.GetSize() > 0 || !newClip.m_Desc.IsEmpty())
		{
			if (newClip.AddToDB(false))
			{
				SaveRemoteIdMapping(newClip.m_id, serverIdStr);
				CString msg;
				msg.Format(_T("MergeRemoteClipToLocal: clip added (ID=%d, CRC=%d, desc='%s')"),
				           newClip.m_id, crc, newClip.m_Desc.Left(50).GetString());
				LogMessage(msg);
				return newClip.m_id;
			}
			else
			{
				LogMessage(_T("MergeRemoteClipToLocal: AddToDB failed"));
			}
		}

		return -1;
	}
	catch (const CppSQLite3Exception& e)
	{
		CString err;
		err.Format(_T("MergeRemoteClipToLocal SQLite error: %hs"), e.errorMessage());
		LogMessage(err);
		return -1;
	}
	catch (const std::exception& e)
	{
		CString err;
		err.Format(_T("MergeRemoteClipToLocal error: %hs"), e.what());
		LogMessage(err);
		return -1;
	}
	catch (...)
	{
		LogMessage(_T("MergeRemoteClipToLocal: unknown error"));
		return -1;
	}
}

// ---------------------------------------------------------------------------
// DeleteLocalClip: Delete a clip from local database (for sync deletions)
// Returns TRUE if clip was found and deleted, FALSE otherwise
// ---------------------------------------------------------------------------
BOOL CCloudSyncManager::DeleteLocalClip(int clipId)
{
	try
	{
		// Check if clip exists
		CSingleLock lockDb(&m_csDb, TRUE);
		CString csCheckSQL;
		csCheckSQL.Format(_T("SELECT lID FROM Main WHERE lID = %d"), clipId);
		
		CppSQLite3Query checkQ = theApp.m_db.execQuery(csCheckSQL);
		if (checkQ.eof())
		{
			// Clip doesn't exist locally, nothing to delete
			return FALSE;
		}

		// Delete clip (this will also trigger the delete_data_trigger to clean up Data table)
		CString csDeleteSQL;
		csDeleteSQL.Format(_T("DELETE FROM Main WHERE lID = %d"), clipId);
		
		theApp.m_db.execDML(csDeleteSQL);

		CString msg;
		msg.Format(_T("DeleteLocalClip: clip %d deleted from local DB"), clipId);
		LogMessage(msg);

		return TRUE;
	}
	catch (const CppSQLite3Exception& e)
	{
		CString err;
		err.Format(_T("DeleteLocalClip SQLite error: %hs"), e.errorMessage());
		LogMessage(err);
		return FALSE;
	}
	catch (const std::exception& e)
	{
		CString err;
		err.Format(_T("DeleteLocalClip error: %hs"), e.what());
		LogMessage(err);
		return FALSE;
	}
	catch (...)
	{
		LogMessage(_T("DeleteLocalClip: unknown error"));
		return FALSE;
	}
}

// ---------------------------------------------------------------------------
// EnsureMappingTable: Create CloudClipMap table if it doesn't exist (M1)
// Maps server-side string clip IDs to local integer clip IDs
// ---------------------------------------------------------------------------
void CCloudSyncManager::EnsureMappingTable()
{
	try
	{
		CSingleLock lockDb(&m_csDb, TRUE);
		CString csSQL;
		csSQL.Format(_T("CREATE TABLE IF NOT EXISTS CloudClipMap (")
		             _T("local_id INTEGER PRIMARY KEY,")
		             _T("remote_id TEXT NOT NULL UNIQUE,")
		             _T("created_at INTEGER DEFAULT (strftime('%%s','now'))")
		             _T(")"));
		theApp.m_db.execDML(csSQL);
		LogMessage(_T("EnsureMappingTable: CloudClipMap table ready."));
	}
	catch (const CppSQLite3Exception& e)
	{
		CString err;
		err.Format(_T("EnsureMappingTable SQLite error: %hs"), e.errorMessage());
		LogMessage(err);
	}
	catch (...)
	{
		LogMessage(_T("EnsureMappingTable: unknown error"));
	}
}

// ---------------------------------------------------------------------------
// SaveRemoteIdMapping: Insert or update a local-to-remote ID mapping (M1)
// ---------------------------------------------------------------------------
void CCloudSyncManager::SaveRemoteIdMapping(int localId, const std::string& remoteId)
{
	if (localId <= 0 || remoteId.empty())
		return;

	try
	{
		CSingleLock lockDb(&m_csDb, TRUE);
		CString csSQL;
		csSQL.Format(_T("INSERT OR REPLACE INTO CloudClipMap (local_id, remote_id) ")
		             _T("VALUES (%d, '%hs')"), localId, remoteId.c_str());
		theApp.m_db.execDML(csSQL);
	}
	catch (const CppSQLite3Exception& e)
	{
		CString err;
		err.Format(_T("SaveRemoteIdMapping SQLite error: %hs"), e.errorMessage());
		LogMessage(err);
	}
	catch (...)
	{
		LogMessage(_T("SaveRemoteIdMapping: unknown error"));
	}
}

// ---------------------------------------------------------------------------
// GetLocalIdByRemoteId: Look up local clip ID by server string ID (M1)
// Returns -1 if not found
// ---------------------------------------------------------------------------
int CCloudSyncManager::GetLocalIdByRemoteId(const std::string& remoteId)
{
	if (remoteId.empty())
		return -1;

	try
	{
		CSingleLock lockDb(&m_csDb, TRUE);
		CString csSQL;
		csSQL.Format(_T("SELECT local_id FROM CloudClipMap WHERE remote_id = '%hs' LIMIT 1"),
		             remoteId.c_str());
		CppSQLite3Query q = theApp.m_db.execQuery(csSQL);
		if (q.eof() == false)
		{
			return q.getIntField(_T("local_id"));
		}
	}
	catch (const CppSQLite3Exception& e)
	{
		CString err;
		err.Format(_T("GetLocalIdByRemoteId SQLite error: %hs"), e.errorMessage());
		LogMessage(err);
	}
	catch (...)
	{
		LogMessage(_T("GetLocalIdByRemoteId: unknown error"));
	}
	return -1;
}

// ---------------------------------------------------------------------------
// GetRemoteIdByLocalId: Look up remote clip ID by local integer ID (M1)
// Returns empty string if not found
// ---------------------------------------------------------------------------
std::string CCloudSyncManager::GetRemoteIdByLocalId(int localId)
{
	if (localId <= 0)
		return std::string();

	try
	{
		CSingleLock lockDb(&m_csDb, TRUE);
		CString csSQL;
		csSQL.Format(_T("SELECT remote_id FROM CloudClipMap WHERE local_id = %d LIMIT 1"), localId);
		CppSQLite3Query q = theApp.m_db.execQuery(csSQL);
		if (q.eof() == false)
		{
			CString remoteId = q.getStringField(_T("remote_id"));
			return CStringToStdString(remoteId);
		}
	}
	catch (const CppSQLite3Exception& e)
	{
		CString err;
		err.Format(_T("GetRemoteIdByLocalId SQLite error: %hs"), e.errorMessage());
		LogMessage(err);
	}
	catch (...)
	{
		LogMessage(_T("GetRemoteIdByLocalId: unknown error"));
	}
	return std::string();
}

// ---------------------------------------------------------------------------
// EnsureGroupMappingTable: Create CloudGroupMap table if it doesn't exist
// ---------------------------------------------------------------------------
void CCloudSyncManager::EnsureGroupMappingTable()
{
	try
	{
		CSingleLock lockDb(&m_csDb, TRUE);
		CString csSQL;
		csSQL.Format(_T("CREATE TABLE IF NOT EXISTS CloudGroupMap (")
		             _T("local_id INTEGER PRIMARY KEY,")
		             _T("remote_id TEXT NOT NULL UNIQUE")
		             _T(")"));
		theApp.m_db.execDML(csSQL);
		LogMessage(_T("EnsureGroupMappingTable: CloudGroupMap table ready."));
	}
	catch (const CppSQLite3Exception& e)
	{
		CString err;
		err.Format(_T("EnsureGroupMappingTable SQLite error: %hs"), e.errorMessage());
		LogMessage(err);
	}
	catch (...)
	{
		LogMessage(_T("EnsureGroupMappingTable: unknown error"));
	}
}

// ---------------------------------------------------------------------------
// SaveRemoteGroupIdMapping: Insert or update a local-to-remote group ID mapping
// ---------------------------------------------------------------------------
void CCloudSyncManager::SaveRemoteGroupIdMapping(int localId, const std::string& remoteId)
{
	if (localId <= 0 || remoteId.empty())
		return;

	try
	{
		CSingleLock lockDb(&m_csDb, TRUE);
		CString csSQL;
		csSQL.Format(_T("INSERT OR REPLACE INTO CloudGroupMap (local_id, remote_id) ")
		             _T("VALUES (%d, '%hs')"), localId, remoteId.c_str());
		theApp.m_db.execDML(csSQL);
	}
	catch (const CppSQLite3Exception& e)
	{
		CString err;
		err.Format(_T("SaveRemoteGroupIdMapping SQLite error: %hs"), e.errorMessage());
		LogMessage(err);
	}
	catch (...)
	{
		LogMessage(_T("SaveRemoteGroupIdMapping: unknown error"));
	}
}

// ---------------------------------------------------------------------------
// GetLocalGroupIdByRemoteId: Look up local group ID by remote string ID
// Returns -1 if not found
// ---------------------------------------------------------------------------
int CCloudSyncManager::GetLocalGroupIdByRemoteId(const std::string& remoteId)
{
	if (remoteId.empty())
		return -1;

	try
	{
		CSingleLock lockDb(&m_csDb, TRUE);
		CString csSQL;
		csSQL.Format(_T("SELECT local_id FROM CloudGroupMap WHERE remote_id = '%hs' LIMIT 1"),
		             remoteId.c_str());
		CppSQLite3Query q = theApp.m_db.execQuery(csSQL);
		if (q.eof() == false)
		{
			return q.getIntField(_T("local_id"));
		}
	}
	catch (const CppSQLite3Exception& e)
	{
		CString err;
		err.Format(_T("GetLocalGroupIdByRemoteId SQLite error: %hs"), e.errorMessage());
		LogMessage(err);
	}
	catch (...)
	{
		LogMessage(_T("GetLocalGroupIdByRemoteId: unknown error"));
	}
	return -1;
}

// ---------------------------------------------------------------------------
// GetRemoteGroupIdByLocalId: Look up remote group ID by local integer ID
// Returns empty string if not found
// ---------------------------------------------------------------------------
std::string CCloudSyncManager::GetRemoteGroupIdByLocalId(int localId)
{
	if (localId <= 0)
		return std::string();

	try
	{
		CSingleLock lockDb(&m_csDb, TRUE);
		CString csSQL;
		csSQL.Format(_T("SELECT remote_id FROM CloudGroupMap WHERE local_id = %d LIMIT 1"), localId);
		CppSQLite3Query q = theApp.m_db.execQuery(csSQL);
		if (q.eof() == false)
		{
			CString remoteId = q.getStringField(_T("remote_id"));
			return CStringToStdString(remoteId);
		}
	}
	catch (const CppSQLite3Exception& e)
	{
		CString err;
		err.Format(_T("GetRemoteGroupIdByLocalId SQLite error: %hs"), e.errorMessage());
		LogMessage(err);
	}
	catch (...)
	{
		LogMessage(_T("GetRemoteGroupIdByLocalId: unknown error"));
	}
	return std::string();
}

// ---------------------------------------------------------------------------
// DeleteRemoteGroupIdMapping: Delete group mapping by local ID
// ---------------------------------------------------------------------------
void CCloudSyncManager::DeleteRemoteGroupIdMapping(int localId)
{
	if (localId <= 0)
		return;

	try
	{
		CSingleLock lockDb(&m_csDb, TRUE);
		CString csSQL;
		csSQL.Format(_T("DELETE FROM CloudGroupMap WHERE local_id = %d"), localId);
		theApp.m_db.execDML(csSQL);
	}
	catch (const CppSQLite3Exception& e)
	{
		CString err;
		err.Format(_T("DeleteRemoteGroupIdMapping SQLite error: %hs"), e.errorMessage());
		LogMessage(err);
	}
	catch (...)
	{
		LogMessage(_T("DeleteRemoteGroupIdMapping: unknown error"));
	}
}

// ---------------------------------------------------------------------------
// DeleteRemoteGroupIdMappingByRemote: Delete group mapping by remote ID
// ---------------------------------------------------------------------------
void CCloudSyncManager::DeleteRemoteGroupIdMappingByRemote(const std::string& remoteId)
{
	if (remoteId.empty())
		return;

	try
	{
		CSingleLock lockDb(&m_csDb, TRUE);
		CString csSQL;
		csSQL.Format(_T("DELETE FROM CloudGroupMap WHERE remote_id = '%hs'"), remoteId.c_str());
		theApp.m_db.execDML(csSQL);
	}
	catch (const CppSQLite3Exception& e)
	{
		CString err;
		err.Format(_T("DeleteRemoteGroupIdMappingByRemote SQLite error: %hs"), e.errorMessage());
		LogMessage(err);
	}
	catch (...)
	{
		LogMessage(_T("DeleteRemoteGroupIdMappingByRemote: unknown error"));
	}
}

// ---------------------------------------------------------------------------
// PushGroups: Push local groups to cloud (creates/updates groups on server)
// ---------------------------------------------------------------------------
std::vector<std::string> CCloudSyncManager::PushGroups()
{
	std::vector<std::string> newGroupIds;
	EnsureHttpClient();
	try
	{
		CSingleLock lockDb(&m_csDb, TRUE);
		CppSQLite3Query q = theApp.m_db.execQuery(
			_T("SELECT lID, mText, m_Description, lParentID FROM Main WHERE bIsGroup = 1 AND (lDontSync IS NULL OR lDontSync = 0)"));

		while (!q.eof())
		{
			int localId = q.getIntField(_T("lID"));
			CString name = q.getStringField(_T("mText"));
			CString desc = q.getStringField(_T("m_Description"));
			int localParentId = q.getIntField(_T("lParentID"));
			std::string remoteId = GetRemoteGroupIdByLocalId(localId);

			// Build parent_id
			std::string parentId;
			if (localParentId > 0)
				parentId = GetRemoteGroupIdByLocalId(localParentId);

			std::string groupName = CStringToStdString(name);
			std::string groupDesc = CStringToStdString(desc);

			nlohmann::json body;
			body["name"] = groupName;
			body["description"] = groupDesc;
			if (!parentId.empty())
				body["parent_id"] = parentId;

			if (remoteId.empty())
			{
				// Create new group
				auto res = m_httpClient->Post("/api/v1/groups", body.dump(), "application/json");
				if (res && res->status == 200)
				{
					try
					{
						auto resp = nlohmann::json::parse(res->body);
						if (resp.contains("data") && resp["data"].contains("id"))
						{
							SaveRemoteGroupIdMapping(localId, resp["data"]["id"].get<std::string>());
							newGroupIds.push_back(resp["data"]["id"].get<std::string>());
						}
					}
					catch (...)
					{
						LogMessage(_T("PushGroups: failed to process group response"));
					}
				}
			}
			else
			{
				// Update existing group
				auto res = m_httpClient->Put(("/api/v1/groups/" + remoteId).c_str(), body.dump(), "application/json");
			}

			q.nextRow();
		}
	}
	catch (...)
	{
	}
	return newGroupIds;
}

// ---------------------------------------------------------------------------
// PullGroups: Pull groups from cloud (creates/updates local groups from server)
// ---------------------------------------------------------------------------
void CCloudSyncManager::PullGroups()
{
	EnsureHttpClient();
	try
	{
		std::set<std::string> seenRemoteIds;

		int page = 1;
		bool hasMore = true;

		while (hasMore)
		{
			std::string url = "/api/v1/groups?page=" + std::to_string(page) + "&per_page=200";
			auto res = m_httpClient->Get(url.c_str());
			if (!res || res->status != 200)
				break;

			try
			{
				auto resp = nlohmann::json::parse(res->body);
				if (!resp.contains("data") || !resp["data"].contains("items"))
					break;

				auto& items = resp["data"]["items"];

				// Phase 1: Create/update all groups
				for (auto& item : items)
				{
					std::string remoteId = item["id"].value("", "");
					std::string name = item["name"].value("", "");
					std::string description = item["description"].value("", "");

					if (remoteId.empty()) continue;

					seenRemoteIds.insert(remoteId);

					int localId = GetLocalGroupIdByRemoteId(remoteId);

					CSingleLock lockDb(&m_csDb, TRUE);

					if (localId <= 0)
					{
						CString csName(name.c_str());
						csName.Replace(_T("'"), _T("''"));
						CString csDesc(description.c_str());
						csDesc.Replace(_T("'"), _T("''"));

						CString csSQL;
						csSQL.Format(_T("INSERT INTO Main (lDate, mText, m_Description, lDontAutoDelete, bIsGroup, lParentID, stickyClipOrder, stickyClipGroupOrder, lDontSync) ")
							_T("VALUES (%lld, '%s', '%s', 0, 1, -1, -(2147483647), -(2147483647), 0)"),
							CTime::GetCurrentTime().GetTime(), csName, csDesc);
						theApp.m_db.execDML(csSQL);

						localId = (int)theApp.m_db.lastRowId();

						if (localId > 0)
							SaveRemoteGroupIdMapping(localId, remoteId);
					}
					else
					{
						CString csName(name.c_str());
						csName.Replace(_T("'"), _T("''"));
						CString csDesc(description.c_str());
						csDesc.Replace(_T("'"), _T("''"));

						CString csSQL;
						csSQL.Format(_T("UPDATE Main SET mText = '%s', m_Description = '%s', lModifiedDate = %lld WHERE lID = %d"),
							csName, csDesc, CTime::GetCurrentTime().GetTime(), localId);
						theApp.m_db.execDML(csSQL);
					}
				}

				// Phase 2: Set parent relationships
				for (auto& item : items)
				{
					std::string remoteId = item["id"].value("", "");
					std::string parentRemoteId = item["parent_id"].value("", "");

					if (remoteId.empty()) continue;
					if (parentRemoteId.empty()) continue;

					int localId = GetLocalGroupIdByRemoteId(remoteId);
					int localParentId = GetLocalGroupIdByRemoteId(parentRemoteId);

					if (localId > 0 && localParentId > 0)
					{
						CSingleLock lockDb(&m_csDb, TRUE);
						CString csSQL;
						csSQL.Format(_T("UPDATE Main SET lParentID = %d WHERE lID = %d"), localParentId, localId);
						theApp.m_db.execDML(csSQL);
					}
				}

				hasMore = resp["data"].value("has_more", false);
				page++;
			}
			catch (...)
			{
				LogMessage(_T("PullGroups: failed to parse page, breaking pagination"));
				break;
			}
		}

		// 清理已不在服务端存在的群组映射
		{
			CSingleLock lockDb(&m_csDb, TRUE);
			CppSQLite3Query q = theApp.m_db.execQuery(_T("SELECT remote_id, local_id FROM CloudGroupMap"));
			while (!q.eof())
			{
				std::string remoteId = q.getStringField(_T("remote_id"), "");
				int localId = q.getIntField(_T("local_id"));
				if (!remoteId.empty() && seenRemoteIds.find(remoteId) == seenRemoteIds.end())
				{
					CString csSQL;
					csSQL.Format(_T("DELETE FROM Main WHERE lID = %d AND bIsGroup = 1"), localId);
					theApp.m_db.execDML(csSQL);
					csSQL.Format(_T("DELETE FROM CloudGroupMap WHERE remote_id = '%hs'"), remoteId.c_str());
					theApp.m_db.execDML(csSQL);
				}
				q.nextRow();
			}
		}
	}
	catch (...)
	{
	}
}

// ---------------------------------------------------------------------------
// DeleteRemoteGroup: Delete remote group by remote ID
// ---------------------------------------------------------------------------
void CCloudSyncManager::DeleteRemoteGroup(const std::string& remoteGroupId)
{
	if (remoteGroupId.empty()) return;
	EnsureHttpClient();
	auto res = m_httpClient->Delete(("/api/v1/groups/" + remoteGroupId).c_str());
	if (res && (res->status == 200 || res->status == 404))
	{
		DeleteRemoteGroupIdMappingByRemote(remoteGroupId);
	}
}

// ---------------------------------------------------------------------------
// MarkClipsDontSync: Mark clips as dont-sync on server
// ---------------------------------------------------------------------------
void CCloudSyncManager::MarkClipsDontSync(const std::vector<int>& localClipIds)
{
	if (localClipIds.empty()) return;
	EnsureHttpClient();

	std::vector<std::string> remoteIds;
	for (int localId : localClipIds)
	{
		std::string remoteId = GetRemoteIdByLocalId(localId);
		if (!remoteId.empty())
			remoteIds.push_back(remoteId);
	}

	if (remoteIds.empty()) return;

	nlohmann::json body;
	body["ids"] = remoteIds;

	auto res = m_httpClient->Post("/api/v1/clips/batch-dont-sync", body.dump(), "application/json");
}

// ---------------------------------------------------------------------------
// DeleteRemoteClips: Notify server to soft-delete clips by remote ID
// ---------------------------------------------------------------------------
void CCloudSyncManager::DeleteRemoteClips(const std::vector<int>& localClipIds)
{
	if (localClipIds.empty()) return;
	EnsureHttpClient();

	std::vector<std::string> remoteIds;
	for (int localId : localClipIds)
	{
		std::string remoteId = GetRemoteIdByLocalId(localId);
		if (!remoteId.empty())
			remoteIds.push_back(remoteId);
	}

	if (remoteIds.empty()) return;

	nlohmann::json body;
	body["ids"] = remoteIds;

	auto res = m_httpClient->Post("/api/v1/clips/batch-delete", body.dump(), "application/json");
	if (res && (res->status == 200 || res->status == 404))
	{
		for (int localId : localClipIds)
		{
			try
			{
				CSingleLock lockDb(&m_csDb, TRUE);
				CString csSQL;
				csSQL.Format(_T("DELETE FROM CloudClipMap WHERE local_id = %d"), localId);
				theApp.m_db.execDML(csSQL);
			}
			catch (...)
			{
				LogMessage(_T("MarkClipsDontSync: failed to delete mapping"));
			}
		}
	}
}

// ---------------------------------------------------------------------------
// BuildWsUrl: Convert HTTP(S) server URL to WebSocket URL (H4)
// ---------------------------------------------------------------------------
CString CCloudSyncManager::BuildWsUrl()
{
	CString wsUrl(m_serverUrl);
	wsUrl.Replace(_T("https://"), _T("wss://"));
	wsUrl.Replace(_T("http://"), _T("ws://"));
	// Append WebSocket endpoint path
	if (wsUrl.Right(1) != _T("/"))
		wsUrl += _T("/");
	wsUrl += _T("api/v1/ws");
	return wsUrl;
}

// ---------------------------------------------------------------------------
// StartWebSocket: Launch WebSocket listener thread (H4)
// ---------------------------------------------------------------------------
void CCloudSyncManager::StartWebSocket()
{
	if (m_pWsThread != nullptr)
	{
		LogMessage(_T("StartWebSocket: WS thread already running."));
		return;
	}

	if (m_deviceToken.IsEmpty())
	{
		LogMessage(_T("StartWebSocket: no device token, skipping."));
		return;
	}

	m_pWsThread = AfxBeginThread(WsThreadProc, this, THREAD_PRIORITY_NORMAL, 0, CREATE_SUSPENDED);
	if (m_pWsThread == nullptr)
	{
		LogMessage(_T("StartWebSocket: failed to create WS thread."));
		return;
	}

	m_pWsThread->m_bAutoDelete = FALSE;
	m_pWsThread->ResumeThread();
	LogMessage(_T("StartWebSocket: WS listener thread started."));
}

// ---------------------------------------------------------------------------
// StopWebSocket: Signal WS thread to stop and clean up (H4)
// ---------------------------------------------------------------------------
void CCloudSyncManager::StopWebSocket()
{
	// Force-close WS connection to interrupt blocking read() in WS thread
	EnterCriticalSection(&m_csWsClient);
	if (m_pWsClient != nullptr)
	{
		auto* wsClient = static_cast<httplib::ws::WebSocketClient*>(m_pWsClient);
		if (wsClient->is_open())
		{
			wsClient->close(httplib::ws::CloseStatus::Normal, "Shutdown");
		}
	}
	LeaveCriticalSection(&m_csWsClient);

	if (m_pWsThread != nullptr)
	{
		// m_hStopEvent is already set by Stop(), WS thread will see it
		DWORD dwWait = WaitForSingleObject(m_pWsThread->m_hThread, 5000);
		if (dwWait == WAIT_TIMEOUT)
		{
			LogMessage(_T("StopWebSocket: WS thread did not exit within timeout."));
		}
		else
		{
			LogMessage(_T("StopWebSocket: WS thread exited cleanly."));
		}

		delete m_pWsThread;
		m_pWsThread = nullptr;
	}

	// Thread has exited, safely clean up WS client
	EnterCriticalSection(&m_csWsClient);
	if (m_pWsClient != nullptr)
	{
		auto* wsClient = static_cast<httplib::ws::WebSocketClient*>(m_pWsClient);
		delete wsClient;
		m_pWsClient = nullptr;
	}
	LeaveCriticalSection(&m_csWsClient);

	InterlockedExchange(&m_wsReconnectDelay, 1000);
}

// ---------------------------------------------------------------------------
// WsThreadProc: WebSocket listener thread (H4)
// Connects to the server, listens for real-time events, signals sync on
// "clip_added" messages. Reconnects with exponential backoff on disconnect.
// ---------------------------------------------------------------------------
UINT CCloudSyncManager::WsThreadProc(LPVOID pParam)
{
	auto* pThis = static_cast<CCloudSyncManager*>(pParam);
	if (pThis == nullptr)
		return 1;

	LogMessage(_T("WsThreadProc: WebSocket listener started."));

	while (true)
	{
		// Check stop event before attempting connection
		if (WaitForSingleObject(pThis->m_hStopEvent, 0) == WAIT_OBJECT_0)
			break;

		// Build WebSocket URL
		CString wsUrlCStr = pThis->BuildWsUrl();
		CStringA wsUrlA(wsUrlCStr);
		std::string wsUrl(wsUrlA.GetString());

		// Prepare auth headers: pass device_token as Sec-WebSocket-Protocol
		httplib::Headers headers = {
			{"Authorization", "Bearer " + std::string(CStringA(pThis->m_deviceToken))}
		};

		auto* wsClient = new httplib::ws::WebSocketClient(wsUrl, headers);
		EnterCriticalSection(&pThis->m_csWsClient);
		pThis->m_pWsClient = wsClient;
		LeaveCriticalSection(&pThis->m_csWsClient);

		if (!wsClient->is_valid())
		{
			LogMessage(_T("WsThreadProc: invalid WS URL, retrying later."));
			delete wsClient;
			EnterCriticalSection(&pThis->m_csWsClient);
			pThis->m_pWsClient = nullptr;
			LeaveCriticalSection(&pThis->m_csWsClient);
			LONG curDelay1 = pThis->m_wsReconnectDelay;
			Sleep(curDelay1);
			LONG newDelay1 = min(curDelay1 * 2, 30000L);
			InterlockedCompareExchange(&pThis->m_wsReconnectDelay, newDelay1, curDelay1);
			continue;
		}

		// Connect to server
		if (!wsClient->connect())
		{
			CString msg;
			msg.Format(_T("WsThreadProc: connection failed, retrying in %d ms"), pThis->m_wsReconnectDelay);
			LogMessage(msg);
			delete wsClient;
			EnterCriticalSection(&pThis->m_csWsClient);
			pThis->m_pWsClient = nullptr;
			LeaveCriticalSection(&pThis->m_csWsClient);
			LONG curDelay2 = pThis->m_wsReconnectDelay;
			Sleep(curDelay2);
			LONG newDelay2 = min(curDelay2 * 2, 30000L);
			InterlockedCompareExchange(&pThis->m_wsReconnectDelay, newDelay2, curDelay2);
			continue;
		}

		// Connected successfully — reset backoff
		InterlockedExchange(&pThis->m_wsReconnectDelay, 1000);
		LogMessage(_T("WsThreadProc: connected to WebSocket server."));

		// Read loop
		while (true)
		{
			// Non-blocking check for stop event
			if (WaitForSingleObject(pThis->m_hStopEvent, 0) == WAIT_OBJECT_0)
			{
				LogMessage(_T("WsThreadProc: stop event received, exiting."));
				break;
			}

			std::string msg;
			auto result = wsClient->read(msg);

			if (result == httplib::ws::ReadResult::Text)
			{
				pThis->OnWsMessage(msg);
			}
			else if (result == httplib::ws::ReadResult::Fail)
			{
				LogMessage(_T("WsThreadProc: connection lost, will reconnect."));
				break;
			}
			// Binary messages are ignored
		}

		// Close connection gracefully
		if (wsClient->is_open())
		{
			wsClient->close(httplib::ws::CloseStatus::Normal, "Client shutting down");
		}
		delete wsClient;
		EnterCriticalSection(&pThis->m_csWsClient);
		pThis->m_pWsClient = nullptr;
		LeaveCriticalSection(&pThis->m_csWsClient);

		// Check stop before reconnecting
		if (WaitForSingleObject(pThis->m_hStopEvent, 0) == WAIT_OBJECT_0)
			break;

		// Exponential backoff before reconnection
		CString msg;
		msg.Format(_T("WsThreadProc: reconnecting in %d ms"), pThis->m_wsReconnectDelay);
		LogMessage(msg);
LONG curDelay = pThis->m_wsReconnectDelay;
			Sleep(curDelay);
			LONG newDelay = min(curDelay * 2, 30000L);
			InterlockedCompareExchange(&pThis->m_wsReconnectDelay, newDelay, curDelay);
	}

	LogMessage(_T("WsThreadProc: WebSocket listener exiting."));
	return 0;
}

// ---------------------------------------------------------------------------
// OnWsMessage: Handle incoming WebSocket message (H4)
// Parses message, triggers sync on "clip_added" events
// ---------------------------------------------------------------------------
void CCloudSyncManager::OnWsMessage(const std::string& msg)
{
	try
	{
		json j = json::parse(msg);
		std::string type = j.value("type", "");

		if (type == "clip_added")
		{
			LogMessage(_T("OnWsMessage: clip_added received, triggering sync."));
			SetEvent(m_hWsTrigger);
		}
		else if (type == "connected")
		{
			LogMessage(_T("OnWsMessage: connected to server (initial handshake)."));
		}
		else if (type == "ping")
		{
			// Server sent ping; httplib handles pong internally, no action needed
		}
		else if (type == "goaway")
		{
			LogMessage(_T("OnWsMessage: server requested disconnect (goaway)."));
		}
		else
		{
			CString msg;
			msg.Format(_T("OnWsMessage: unhandled message type '%hs'"), type.c_str());
			LogMessage(msg);
		}
	}
	catch (const json::parse_error& e)
	{
		CString err;
		err.Format(_T("OnWsMessage: JSON parse error: %hs"), e.what());
		LogMessage(err);
	}
	catch (...)
	{
		LogMessage(_T("OnWsMessage: unknown error"));
	}
}

void CCloudSyncManager::TriggerQuickSync()
{
	EnterCriticalSection(&m_csSync);
	BOOL bShouldSync = (m_pSyncThread != nullptr && m_hStopEvent != nullptr);
	if (bShouldSync)
	{
		m_nActiveQuickSyncThreads++;
	}
	LeaveCriticalSection(&m_csSync);

	if (!bShouldSync)
	{
		OutputDebugStringA("[CloudSync] TriggerQuickSync: sync not running.\n");
		return;
	}

	QuickSyncContext* ctx = new QuickSyncContext;
	ctx->pManager = this;
	ctx->pCounter = &m_nActiveQuickSyncThreads;
	ctx->pCS = &m_csSync;

	CWinThread* pThread = AfxBeginThread(QuickSyncThreadProc, ctx, THREAD_PRIORITY_NORMAL, 0, 0);
	if (pThread)
	{
		OutputDebugStringA("[CloudSync] TriggerQuickSync: spawned quick-push thread.\n");
	}
	else
	{
		delete ctx;
	}
}

void CCloudSyncManager::OnGroupDeleted(int localGroupId)
{
	LogMessage(_T("OnGroupDeleted: removing group mapping and triggering quick sync."));
	DeleteRemoteGroupIdMapping(localGroupId);
	TriggerQuickSync();
}
