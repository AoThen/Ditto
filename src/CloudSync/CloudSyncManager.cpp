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

using json = nlohmann::json;

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

CCloudSyncManager::CCloudSyncManager()
	: m_hStopEvent(nullptr)
	, m_hWsTrigger(nullptr)
	, m_pSyncThread(nullptr)
	, m_pWsThread(nullptr)
	, m_cryptoInitialized(FALSE)
	, m_lastSyncTime(0)
	, m_nActiveQuickSyncThreads(0)
	, m_pWsClient(nullptr)
	, m_wsReconnectDelay(1000)
{
	InitializeCriticalSection(&m_csSync);
}

CCloudSyncManager::~CCloudSyncManager()
{
	Stop();
	DeleteCriticalSection(&m_csSync);
}

BOOL CCloudSyncManager::Initialize()
{
	m_serverUrl = CGetSetOptions::GetCloudServerUrl();
	if (m_serverUrl.IsEmpty())
	{
		m_serverUrl = _T("https://localhost:8080");
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

void CCloudSyncManager::Stop()
{
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
		pThis->PushNewClips();
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
	PushNewClips();
	PullChanges();
}

BOOL CCloudSyncManager::IsLoggedIn() const
{
	return CCloudAuth::IsLoggedIn();
}

BOOL CCloudSyncManager::IsEncryptionEnabled() const
{
	return m_cryptoInitialized;
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
			m_cryptoInitialized = TRUE;
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
	if (!m_cryptoInitialized)
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
	if (!m_cryptoInitialized)
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
		std::string url = CStringToStdString(m_serverUrl);
		httplib::Client cli(url);
		cli.set_connection_timeout(5, 0);
		cli.set_read_timeout(5, 0);
		cli.set_default_headers({
			{"Authorization", "Bearer " + std::string(CStringA(m_deviceToken))}
		});

		auto res = cli.Get("/api/v1/encryption/salt");
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
		// Silent fail
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
		// Wait for stop event, WS trigger, or timeout (30 second sync interval)
		DWORD dwResult = WaitForMultipleObjects(2, waitHandles, FALSE, 30000);
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

		// Check if auto sync is enabled
		if (!CGetSetOptions::GetCloudAutoSync())
		{
			continue;
		}

		// Sync interval elapsed or WS triggered, perform sync
		try
		{
			pThis->CheckAndNotifyEncryptionChange();
			pThis->PushNewClips();
			pThis->PullChanges();
		}
		catch (const std::exception& e)
		{
			CString err;
			err.Format(_T("Sync error: %hs"), e.what());
			LogMessage(err);
		}
		catch (...)
		{
			LogMessage(_T("Unknown sync error."));
		}
	}

	LogMessage(_T("Background sync thread exiting."));
	return 0;
}

void CCloudSyncManager::PushNewClips()
{
	try
	{
		LogMessage(_T("PushNewClips: checking for new/modified clips since last sync..."));

		// Enumerate local clips modified since last sync (thread-safe read)
		time_t lastSync;
		EnterCriticalSection(&m_csSync);
		lastSync = m_lastSyncTime;
		LeaveCriticalSection(&m_csSync);

		json clipsArray;
		if (!GetLocalClipsSince(lastSync, clipsArray))
		{
			LogMessage(_T("PushNewClips: failed to enumerate local clips."));
			return;
		}

		if (clipsArray.empty())
		{
			LogMessage(_T("PushNewClips: no new clips to push."));
			return;
		}

		// Build sync request JSON matching server's PushClipItem schema:
	 // Server expects: { since, device_id, push_clips: [{ id, description, crc,
		// group_id, short_cut, updated_at, formats: [{format_type, data}] }] }
		json syncReq;
		if (lastSync > 0)
		{
			// Format as RFC3339: "2025-01-01T00:00:00Z"
			SYSTEMTIME st;
			FILETIME ft;
			ULARGE_INTEGER uli;
			uli.QuadPart = ((ULONGLONG)lastSync * 10000000ULL) + 116444736000000000ULL;
			ft.dwLowDateTime = uli.LowPart;
			ft.dwHighDateTime = uli.HighPart;
			FileTimeToSystemTime(&ft, &st);
			char timeBuf[32];
			sprintf_s(timeBuf, "%04d-%02d-%02dT%02d:%02d:%02dZ",
			          st.wYear, st.wMonth, st.wDay,
			          st.wHour, st.wMinute, st.wSecond);
			syncReq["since"] = std::string(timeBuf);
		}
		else
		{
			syncReq["since"] = "1970-01-01T00:00:00Z";
		}

		syncReq["device_id"] = std::string(m_deviceId);
		syncReq["push_clips"] = clipsArray;

		// Send to server
		CStringA serverUrlA(m_serverUrl);
		std::string url = serverUrlA.GetString();
		httplib::Client cli(url);
		// Configure timeouts: 10s connection, 30s read, 30s write
		cli.set_connection_timeout(10, 0);
		cli.set_read_timeout(30, 0);
		cli.set_write_timeout(30, 0);
		cli.set_default_headers({
			{"Authorization", "Bearer " + std::string(CStringA(m_deviceToken))}
		});

		std::string bodyStr = syncReq.dump();
		auto res = cli.Post("/api/v1/clips/sync", bodyStr, "application/json");
		if (!res)
		{
			LogMessage(_T("PushNewClips: failed to connect to server"));
			return;
		}

		// Handle authentication errors (401/403) - trigger re-auth flow
		if (res->status == 401 || res->status == 403)
		{
			LogMessage(_T("PushNewClips: token expired or invalid, clearing token for re-auth."));
			
			// Clear stored credentials
			CCloudAuth::Logout();
			
			// Notify user via main window (post message to avoid blocking sync thread)
			CWnd* pMainWnd = AfxGetMainWnd();
			if (pMainWnd != nullptr)
			{
				// Post custom message with 401 status code
				::PostMessage(pMainWnd->GetSafeHwnd(), WM_CLOUD_AUTH_REQUIRED, 401, 0);
			}
			
			LogMessage(_T("PushNewClips: posted WM_CLOUD_AUTH_REQUIRED message to main window"));
			return;
		}

		if (res->status == 200)
		{
			try
			{
				json responseJson = json::parse(res->body);

				// Server wraps response in { code: 0, data: { ... } }
				if (responseJson.contains("code") && responseJson["code"].get<int>() != 0)
				{
					CString msg;
					msg.Format(_T("PushNewClips: server error code %d"), responseJson["code"].get<int>());
					LogMessage(msg);
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
					dataNode = &responseJson; // Fallback: use top-level if no data wrapper
				}

				int syncedCount = dataNode->value("updated_count", 0);
				int skippedCount = dataNode->value("skipped_count", 0);

				// Update last sync time from server's sync_time (more accurate than local time)
				if (dataNode->contains("sync_time"))
				{
					std::string syncTimeStr = (*dataNode)["sync_time"].get<std::string>();
					SYSTEMTIME st = {};
					if (sscanf_s(syncTimeStr.c_str(), "%04d-%02d-%02dT%02d:%02d:%02dZ",
						&st.wYear, &st.wMonth, &st.wDay,
						&st.wHour, &st.wMinute, &st.wSecond) == 6)
					{
						FILETIME ft;
						SystemTimeToFileTime(&st, &ft);
						ULARGE_INTEGER uli;
						uli.LowPart = ft.dwLowDateTime;
						uli.HighPart = ft.dwHighDateTime;
						// FILETIME epoch offset: 116444736000000000 (100ns intervals since 1601)
						time_t serverTime = static_cast<time_t>((uli.QuadPart - 116444736000000000ULL) / 10000000ULL);

						EnterCriticalSection(&m_csSync);
						m_lastSyncTime = serverTime;
						LeaveCriticalSection(&m_csSync);
						CGetSetOptions::SetCloudLastSyncTime((__int64)serverTime);
					}
				}

				CString msg;
				msg.Format(_T("PushNewClips: %d clips synced, %d skipped (CRC duplicates)"), syncedCount, skippedCount);
				LogMessage(msg);
			}
			catch (const json::parse_error& e)
			{
				CString err;
				err.Format(_T("PushNewClips: JSON parse error: %hs"), e.what());
				LogMessage(err);
			}
		}
		else
		{
			CString err;
			err.Format(_T("PushNewClips: server returned HTTP %d"), res->status);
			LogMessage(err);
		}
	}
	catch (const std::exception& e)
	{
		CString err;
		err.Format(_T("PushNewClips error: %hs"), e.what());
		LogMessage(err);
	}
	catch (...)
	{
		LogMessage(_T("PushNewClips: unknown error"));
	}
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
		OutputDebugStringA("[CloudSync] Exception in ExtractFilePathsFromHDROP.\n");
	}

	return paths;
}

// ---------------------------------------------------------------------------
// FilterHDROPForSync: replace CF_HDROP format with path-only metadata
// This ensures file contents are NEVER synced, only file paths
// ---------------------------------------------------------------------------
BOOL CCloudSyncManager::FilterHDROPForSync(nlohmann::json& formats)
{
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

				return TRUE;
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

	return FALSE;
}

void CCloudSyncManager::PullChanges()
{
	try
	{
		LogMessage(_T("PullChanges: checking for changes from other devices..."));

		// Build since timestamp as RFC3339
		CStringA sinceStr;
		if (m_lastSyncTime > 0)
		{
			CTime sinceTime((time_t)m_lastSyncTime);
			sinceStr.Format("%04d-%02d-%02dT%02d:%02d:%02dZ",
				sinceTime.GetYear(), sinceTime.GetMonth(), sinceTime.GetDay(),
				sinceTime.GetHour(), sinceTime.GetMinute(), sinceTime.GetSecond());
		}
		else
		{
			sinceStr = "1970-01-01T00:00:00Z";
		}

		// Use GET /clips/changes for pull-only (server has a dedicated pull endpoint)
		CStringA serverUrlA(m_serverUrl);
		std::string url = serverUrlA.GetString();
		httplib::Client cli(url);
		cli.set_connection_timeout(10, 0);
		cli.set_read_timeout(30, 0);
		cli.set_write_timeout(30, 0);
		cli.set_default_headers({
			{"Authorization", "Bearer " + std::string(CStringA(m_deviceToken))}
		});

		// GET /api/v1/clips/changes?since=...
		CStringA path;
		path.Format("/api/v1/clips/changes?since=%s", (LPCSTR)sinceStr);
		auto res = cli.Get(path.GetString());
		if (!res)
		{
			LogMessage(_T("PullChanges: failed to connect to server"));
			return;
		}

		// Handle authentication errors (401/403) - trigger re-auth flow
		if (res->status == 401 || res->status == 403)
		{
			LogMessage(_T("PullChanges: token expired or invalid, clearing token for re-auth."));
			
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
			if (hasClips)
			{
				for (const auto& clip : *clipsNode)
				{
					// Decrypt formats if encryption is enabled
					json formats = clip.contains("formats") ? clip["formats"] : json::array();
					if (m_cryptoInitialized && !formats.empty())
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
					int newId = MergeRemoteClipToLocal(clip);
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

			// Update last sync time from server's sync_time (or use current time as fallback)
			time_t newSyncTime = time(nullptr);
			if (dataNode->contains("server_time"))
			{
				std::string serverTimeStr = (*dataNode)["server_time"].get<std::string>();
				SYSTEMTIME st = {};
				if (sscanf_s(serverTimeStr.c_str(), "%04d-%02d-%02dT%02d:%02d:%02dZ",
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
				if (sscanf_s(syncTimeStr.c_str(), "%04d-%02d-%02dT%02d:%02d:%02dZ",
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
		}
	}
	catch (const std::exception& e)
	{
		CString err;
		err.Format(_T("PullChanges error: %hs"), e.what());
		LogMessage(err);
	}
	catch (...)
	{
		LogMessage(_T("PullChanges: unknown error"));
	}
}

// ---------------------------------------------------------------------------
// GetLocalClipsSince: Enumerate local clips modified since lastSyncTime
// Produces JSON matching server's PushClipItem schema:
//   { id, description, crc, group_id, short_cut, updated_at, formats: [{format_type, data}] }
// ---------------------------------------------------------------------------
BOOL CCloudSyncManager::GetLocalClipsSince(time_t sinceTime, nlohmann::json& clipsArray)
{
	clipsArray = nlohmann::json::array();

	try
	{
		// Query clips modified since last sync
		// Use lModifiedDate (modification time) instead of lDate (creation time)
		// This ensures modified clips are synced even if their creation time is old
		// For initial sync, get the last 100 clips
		CString csSQL;
		if (sinceTime > 0)
		{
			csSQL.Format(_T("SELECT lID, lDate, mText, CRC, bIsGroup, lParentID, ")
			             _T("clipOrder, stickyClipOrder, lShortCut, globalShortCut, ")
			             _T("lDontAutoDelete, lastPasteDate, lModifiedDate ")
			             _T("FROM Main WHERE lModifiedDate > %lld AND bIsGroup = 0 ")
			             _T("ORDER BY lModifiedDate DESC LIMIT 100"), sinceTime);
		}
		else
		{
			// First sync: get recent clips
			csSQL.Format(_T("SELECT lID, lDate, mText, CRC, bIsGroup, lParentID, ")
			             _T("clipOrder, stickyClipOrder, lShortCut, globalShortCut, ")
			             _T("lDontAutoDelete, lastPasteDate, lModifiedDate ")
			             _T("FROM Main WHERE bIsGroup = 0 ")
			             _T("ORDER BY lModifiedDate DESC LIMIT 100"));
		}

		CppSQLite3Query q = theApp.m_db.execQuery(csSQL);

		int clipCount = 0;
		while(q.eof() == false)
		{
			int clipId = q.getIntField(_T("lID"));
			time_t lDate = (time_t)q.getInt64Field(_T("lDate"));
			CString desc = q.getStringField(_T("mText"));
			DWORD crc = (DWORD)q.getIntField(_T("CRC"));
			time_t modDate = (time_t)q.getInt64Field(_T("lModifiedDate"));

			// Build clip JSON matching server's PushClipItem schema
			json clipJson;
			clipJson["id"] = std::to_string(clipId);                 // Server expects "id" (string)
			clipJson["description"] = CStringToStdString(desc);      // Server expects "description"
			clipJson["crc"] = static_cast<int64_t>(crc);             // Server expects "crc" (int64)
			clipJson["group_id"] = "";                               // Server expects "group_id"
			clipJson["short_cut"] = q.getIntField(_T("lShortCut")); // Server expects "short_cut"

			// Ensure mapping entry exists for this pushed clip (M1)
			// This maps the local integer ID to its string representation used by the server
			SaveRemoteIdMapping(clipId, std::to_string(clipId));

			// Server uses updated_at for LWW conflict resolution
			// Use lModifiedDate (modification time) as the authoritative timestamp
			time_t updatedAt = (modDate > 0) ? modDate : lDate;
			if (updatedAt > 0)
			{
				struct tm gmtm;
				gmtime_s(&gmtm, &updatedAt);
				char timeBuf[32];
				strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%SZ", &gmtm);
				clipJson["updated_at"] = std::string(timeBuf);
			}

			// Load formats for this clip
			json formatsArray;
			if (LoadClipFormats(clipId, formatsArray))
			{
				// Filter HDROP formats (file paths only, no content)
				FilterHDROPForSync(formatsArray);

				// Build formats array matching server's PushFormatItem: [{format_type, data}]
				json serverFormats;
				for (const auto& fmt : formatsArray)
				{
					json serverFmt;
					serverFmt["format_type"] = fmt.value("format_type", 0);
					serverFmt["data"] = fmt.value("data", "");  // Already base64 or plain text
					serverFormats.push_back(serverFmt);
				}
				clipJson["formats"] = serverFormats;

				// Encrypt formats if encryption is enabled
				if (m_cryptoInitialized && !serverFormats.empty())
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
			clipCount++;

			q.nextRow();
		}

		CString msg;
		msg.Format(_T("GetLocalClipsSince: found %d clips to sync"), clipCount);
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

		CppSQLite3Query q = theApp.m_db.execQuery(csSQL);

		while(q.eof() == false)
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
int CCloudSyncManager::MergeRemoteClipToLocal(const nlohmann::json& remoteClip)
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
			if (sscanf_s(updatedAtStr.c_str(), "%04d-%02d-%02dT%02d:%02d:%02dZ",
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
			CString csSQL;
			csSQL.Format(_T("SELECT lID, lModifiedDate FROM Main WHERE CRC = ? AND bIsGroup = 0 LIMIT 1"));
			
			CppSQLite3Statement stmt = theApp.m_db.compileStatement(csSQL);
			stmt.bind(1, (int64_t)crc);
			CppSQLite3Query q = stmt.execQuery();
			
			if (q.eof() == false)
			{
				existingId = q.getIntField(_T("lID"));
				localModDate = (time_t)q.getInt64Field(_T("lModifiedDate"));
			}
		}
		catch (...)
		{
			// Clip doesn't exist, will create new one
		}

		if (existingId > 0)
		{
			// Clip with same content (CRC) already exists locally
			// LWW: If remote is significantly newer, update the local clip
			if (remoteUpdatedAt > localModDate + 1)
			{
				// Remote clip is newer - update local clip's modification time
				// (Content is same per CRC match, so no need to update formats)
				CString csUpdateSQL;
				csUpdateSQL.Format(_T("UPDATE Main SET lModifiedDate = %lld WHERE lID = %d"),
				                   (__int64)remoteUpdatedAt, existingId);
				theApp.m_db.execDML(csUpdateSQL);

				CString msg;
				msg.Format(_T("MergeRemoteClipToLocal: clip %d exists, remote newer (CRC match), updated timestamp"), existingId);
				LogMessage(msg);
			}
			else
			{
				// Local clip is same age or newer - skip (LWW: local wins)
				CString msg;
				msg.Format(_T("MergeRemoteClipToLocal: duplicate clip (CRC=%d, local=%lld, remote=%lld), skipping (LWW: local wins)"),
				           crc, (long long)localModDate, (long long)remoteUpdatedAt);
				LogMessage(msg);
			}
			SaveRemoteIdMapping(existingId, serverIdStr);
			return existingId;
		}

		// No CRC match - check if description matches (fallback for clips without CRC)
		if (!desc.empty())
		{
			try
			{
				// Use parameterized query to prevent SQL injection
				CString csSQL;
				csSQL.Format(_T("SELECT lID, lModifiedDate, CRC FROM Main WHERE mText = ? AND bIsGroup = 0 LIMIT 1"));
				
				CppSQLite3Statement stmt = theApp.m_db.compileStatement(csSQL);
				stmt.bind(1, desc.c_str());
				CppSQLite3Query q = stmt.execQuery();
				
				if (q.eof() == false)
				{
					int descMatchId = q.getIntField(_T("lID"));
					localModDate = (time_t)q.getInt64Field(_T("lModifiedDate"));
					DWORD localCRC = (DWORD)q.getIntField(_T("CRC"));

					// Same description but different CRC -> different content
					// LWW: only skip if local is same age or newer
					if (remoteUpdatedAt <= localModDate + 1)
					{
						CString msg;
						msg.Format(_T("MergeRemoteClipToLocal: same description, different CRC, local is newer (local=%lld, remote=%lld), skipping"),
						           (long long)localModDate, (long long)remoteUpdatedAt);
						LogMessage(msg);
						SaveRemoteIdMapping(descMatchId, serverIdStr);
						return descMatchId;
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
				// No description match or error - will create new clip
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
					// UTF-8 text -> convert to UTF-16 for Windows clipboard
					CStringA utf8Data(dataStr.c_str());
					// For UTF-16, store the UTF-8 text directly (Ditto handles conversion)
					hGlobal = GlobalAlloc(GMEM_MOVEABLE, utf8Data.GetLength() + 1);
					if (hGlobal)
					{
						char* pData = (char*)GlobalLock(hGlobal);
						memcpy(pData, utf8Data.GetString(), utf8Data.GetLength() + 1);
						GlobalUnlock(hGlobal);
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
	// First, force-close the WS connection to interrupt any blocking read()
	if (m_pWsClient != nullptr)
	{
		auto* wsClient = static_cast<httplib::WebSocketClient*>(m_pWsClient);
		if (wsClient->is_open())
		{
			wsClient->close(httplib::CloseStatus::Normal, "Shutdown");
		}
	}

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

	// Clean up WS client
	if (m_pWsClient != nullptr)
	{
		auto* wsClient = static_cast<httplib::WebSocketClient*>(m_pWsClient);
		delete wsClient;
		m_pWsClient = nullptr;
	}

	m_wsReconnectDelay = 1000; // Reset backoff
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

		auto* wsClient = new httplib::WebSocketClient(wsUrl, headers);
		pThis->m_pWsClient = wsClient;

		if (!wsClient->is_valid())
		{
			LogMessage(_T("WsThreadProc: invalid WS URL, retrying later."));
			delete wsClient;
			pThis->m_pWsClient = nullptr;
			Sleep(pThis->m_wsReconnectDelay);
			pThis->m_wsReconnectDelay = min(pThis->m_wsReconnectDelay * 2, 30000);
			continue;
		}

		// Connect to server
		if (!wsClient->connect())
		{
			CString msg;
			msg.Format(_T("WsThreadProc: connection failed, retrying in %d ms"), pThis->m_wsReconnectDelay);
			LogMessage(msg);
			delete wsClient;
			pThis->m_pWsClient = nullptr;
			Sleep(pThis->m_wsReconnectDelay);
			pThis->m_wsReconnectDelay = min(pThis->m_wsReconnectDelay * 2, 30000);
			continue;
		}

		// Connected successfully — reset backoff
		pThis->m_wsReconnectDelay = 1000;
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

			if (result == httplib::ReadResult::Text)
			{
				pThis->OnWsMessage(msg);
			}
			else if (result == httplib::ReadResult::Fail)
			{
				LogMessage(_T("WsThreadProc: connection lost, will reconnect."));
				break;
			}
			// Binary messages are ignored
		}

		// Close connection gracefully
		if (wsClient->is_open())
		{
			wsClient->close(httplib::CloseStatus::Normal, "Client shutting down");
		}
		delete wsClient;
		pThis->m_pWsClient = nullptr;

		// Check stop before reconnecting
		if (WaitForSingleObject(pThis->m_hStopEvent, 0) == WAIT_OBJECT_0)
			break;

		// Exponential backoff before reconnection
		CString msg;
		msg.Format(_T("WsThreadProc: reconnecting in %d ms"), pThis->m_wsReconnectDelay);
		LogMessage(msg);
		Sleep(pThis->m_wsReconnectDelay);
		pThis->m_wsReconnectDelay = min(pThis->m_wsReconnectDelay * 2, 30000);
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
