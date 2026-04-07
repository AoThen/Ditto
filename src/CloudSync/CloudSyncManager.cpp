#include "stdafx.h"
#include "CloudSyncManager.h"
#include "CloudAuth.h"
#include "CloudCrypto.h"
#include "CloudEncryption.h"
#include "../httplib.h"
#include "../json.hpp"
#include "../Options.h"

using json = nlohmann::json;

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
	, m_pSyncThread(nullptr)
	, m_cryptoInitialized(FALSE)
{
}

CCloudSyncManager::~CCloudSyncManager()
{
	Stop();
}

BOOL CCloudSyncManager::Initialize()
{
	m_serverUrl = CGetSetOptions::GetCloudServerUrl();
	if (m_serverUrl.IsEmpty())
	{
		m_serverUrl = _T("https://localhost:8080");
	}
	m_deviceToken = CGetSetOptions::GetCloudDeviceToken();

	if (!CCloudAuth::IsLoggedIn())
	{
		OutputDebugString(_T("[CloudSync] Not logged in, skipping sync initialization.\n"));
		return FALSE;
	}

	// Initialize encryption (best effort, log warning if fails)
	if (!InitializeEncryption())
	{
		OutputDebugString(_T("[CloudSync] WARNING: Encryption initialization failed, continuing without encryption.\n"));
	}

	// Create stop event
	m_hStopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	if (m_hStopEvent == nullptr)
	{
		OutputDebugString(_T("[CloudSync] Failed to create stop event.\n"));
		return FALSE;
	}

	// Create sync thread (suspended, then resumed)
	m_pSyncThread = AfxBeginThread(SyncThreadProc, this, THREAD_PRIORITY_NORMAL, 0, CREATE_SUSPENDED);
	if (m_pSyncThread == nullptr)
	{
		OutputDebugString(_T("[CloudSync] Failed to create sync thread.\n"));
		CloseHandle(m_hStopEvent);
		m_hStopEvent = nullptr;
		return FALSE;
	}

	m_pSyncThread->m_bAutoDelete = FALSE;
	m_pSyncThread->ResumeThread();

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
		// Wait up to 10 seconds for thread to exit
		DWORD dwWait = WaitForSingleObject(m_pSyncThread->m_hThread, 10000);
		if (dwWait == WAIT_TIMEOUT)
		{
			// Thread didn't exit in time, terminate it
			TerminateThread(m_pSyncThread->m_hThread, 1);
			OutputDebugString(_T("[CloudSync] Sync thread terminated forcefully.\n"));
		}
		else
		{
			OutputDebugString(_T("[CloudSync] Sync thread exited cleanly.\n"));
		}

		delete m_pSyncThread;
		m_pSyncThread = nullptr;
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
	LogMessage(_T("Clip added (TODO: queue for sync)."));
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

UINT CCloudSyncManager::SyncThreadProc(LPVOID pParam)
{
	CCloudSyncManager* pThis = static_cast<CCloudSyncManager*>(pParam);
	if (pThis == nullptr || pThis->m_hStopEvent == nullptr)
	{
		return 1;
	}

	LogMessage(_T("Background sync thread started."));

	while (true)
	{
		// Wait for stop event or timeout (30 second sync interval)
		DWORD dwResult = WaitForSingleObject(pThis->m_hStopEvent, 30000);
		if (dwResult == WAIT_OBJECT_0)
		{
			// Stop event was signaled
			break;
		}

		// Check if auto sync is enabled
		if (!CGetSetOptions::GetCloudAutoSync())
		{
			continue;
		}

		// Sync interval elapsed, perform sync
		try
		{
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
		// TODO: Implement full push flow once local clip enumeration is available
		// For now, this is a placeholder that shows the encryption flow

		LogMessage(_T("PushNewClips: checking for new/modified clips since last sync..."));

		// TODO: Enumerate local clips modified since last sync
		// std::vector<CClip*> newClips = GetLocalClipsSince(lastSyncTime);
		// if (newClips.empty()) return;

		// Build sync request JSON
		json syncReq;
		syncReq["since"] = "1970-01-01T00:00:00Z"; // TODO: use actual last sync time
		syncReq["device_id"] = "device-1";           // TODO: use actual device ID
		syncReq["push_clips"] = json::array();

		// Example clip push (will be replaced with actual local clips)
		// json clipJson;
		// clipJson["id"] = "clip-uuid";
		// clipJson["description"] = "copied text";
		// clipJson["crc"] = 123456;
		// clipJson["formats"] = ...;
		// if (m_cryptoInitialized)
		// {
		//     if (!EncryptClipFormats(clipJson["formats"]))
		//     {
		//         LogMessage(_T("PushNewClips: encryption failed, skipping clip."));
		//         return;
		//     }
		// }
		// syncReq["push_clips"].push_back(clipJson);

		if (syncReq["push_clips"].empty())
		{
			// No clips to push
			return;
		}

		// Send to server
		CStringA serverUrlA(m_serverUrl);
		std::string url = serverUrlA.GetString();
		httplib::Client cli(url);
		cli.enable_server_certificate_verification(false);
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

		if (res->status == 200)
		{
			LogMessage(_T("PushNewClips: clips pushed successfully"));
			// TODO: Update local sync markers
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

		// Build sync request to pull changes
		json syncReq;
		syncReq["since"] = "1970-01-01T00:00:00Z"; // TODO: use actual last sync time
		syncReq["device_id"] = "device-1";           // TODO: use actual device ID
		syncReq["push_clips"] = json::array();       // Empty push for pull-only

		CStringA serverUrlA(m_serverUrl);
		std::string url = serverUrlA.GetString();
		httplib::Client cli(url);
		cli.enable_server_certificate_verification(false);
		cli.set_default_headers({
			{"Authorization", "Bearer " + std::string(CStringA(m_deviceToken))}
		});

		std::string bodyStr = syncReq.dump();
		auto res = cli.Post("/api/v1/clips/sync", bodyStr, "application/json");
		if (!res)
		{
			LogMessage(_T("PullChanges: failed to connect to server"));
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
			if (responseJson.contains("code") && responseJson["code"].get<int>() != 0)
			{
				LogMessage(_T("PullChanges: server returned error"));
				return;
			}

			if (!responseJson.contains("data") || !responseJson["data"].contains("new_clips"))
			{
				LogMessage(_T("PullChanges: no new clips"));
				return;
			}

			const auto& newClips = responseJson["data"]["new_clips"];
			if (newClips.empty())
			{
				LogMessage(_T("PullChanges: no new clips from other devices"));
				return;
			}

			// Process each new clip
			for (const auto& clip : newClips)
			{
				// Decrypt formats if encryption is enabled
				json formats = clip.contains("formats") ? clip["formats"] : json::array();
				if (m_cryptoInitialized && !formats.empty())
				{
					// Note: Server-side doesn't mark encrypted clips, so we try decrypting
					// If decryption fails, the data is assumed to be unencrypted
					for (auto& format : formats)
					{
						if (format.contains("data") && format["data"].is_string())
						{
							std::string encryptedData = format["data"].get<std::string>();
							CStringA encrypted(encryptedData.c_str());
							CStringA decrypted = CCloudCrypto::Decrypt(encrypted);
							if (!decrypted.IsEmpty())
							{
								// Decryption succeeded, replace with decrypted data
								format["data"] = decrypted.GetString();
								format["decrypted"] = true;
							}
							// If decryption failed, leave data as-is (may be unencrypted)
						}
					}
				}

				// TODO: Merge clip into local database
				// CString desc = CString(clip.value("description", "").c_str());
				// LogMessage(CString(_T("PullChanges: new clip - ")) + desc);
			}

			CString msg;
			msg.Format(_T("PullChanges: received %d new clips"), newClips.size());
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
