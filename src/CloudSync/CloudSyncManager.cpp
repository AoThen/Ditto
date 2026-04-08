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

// Helper: convert CString to std::string
static std::string CStringToStdString(const CString& str)
{
	CT2A utf8(str, CP_UTF8);
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
	, m_pSyncThread(nullptr)
	, m_cryptoInitialized(FALSE)
	, m_lastSyncTime(0)
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
	
	// Trigger an immediate sync when a new clip is added
	// This ensures the new clip is pushed to the cloud quickly
	LogMessage(_T("Clip added - triggering cloud sync."));
	
	// Perform sync in a separate thread to avoid blocking the main thread
	AfxBeginThread([](LPVOID pParam) -> UINT {
		CCloudSyncManager* pThis = static_cast<CCloudSyncManager*>(pParam);
		if (pThis)
		{
			pThis->PushNewClips();
		}
		return 0;
	}, this, THREAD_PRIORITY_NORMAL, 0, 0);
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
		LogMessage(_T("PushNewClips: checking for new/modified clips since last sync..."));

		// Enumerate local clips modified since last sync
		json clipsArray;
		if (!GetLocalClipsSince(m_lastSyncTime, clipsArray))
		{
			LogMessage(_T("PushNewClips: failed to enumerate local clips."));
			return;
		}

		if (clipsArray.empty())
		{
			LogMessage(_T("PushNewClips: no new clips to push."));
			return;
		}

		// Build sync request JSON
		json syncReq;
		syncReq["since"] = (m_lastSyncTime > 0) ? 
			std::string(ctime(&m_lastSyncTime)) : "1970-01-01T00:00:00Z";
		
		// Remove trailing newline from ctime
		std::string& sinceStr = syncReq["since"].get_ref<std::string&>();
		if (!sinceStr.empty() && sinceStr.back() == '\n')
			sinceStr.pop_back();

		// Get device name from settings
		CString deviceName = CGetSetOptions::GetCloudDeviceName();
		if (deviceName.IsEmpty())
		{
			TCHAR buffer[MAX_COMPUTERNAME_LENGTH + 1];
			DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
			GetComputerName(buffer, &size);
			deviceName = buffer;
		}
		syncReq["device_id"] = CStringToStdString(deviceName);
		syncReq["push_clips"] = clipsArray;

		// Send to server
		CStringA serverUrlA(m_serverUrl);
		std::string url = serverUrlA.GetString();
		httplib::Client cli(url);
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
			try
			{
				json responseJson = json::parse(res->body);
				int syncedCount = responseJson.value("synced_count", 0);
				int skippedCount = responseJson.value("skipped_count", 0);
				
				CString msg;
				msg.Format(_T("PushNewClips: %d clips synced, %d skipped (duplicates)"), syncedCount, skippedCount);
				LogMessage(msg);

				// Update last sync time
				m_lastSyncTime = time(nullptr);
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

		// Build sync request to pull changes
		json syncReq;
		syncReq["since"] = "1970-01-01T00:00:00Z"; // TODO: use actual last sync time
		syncReq["device_id"] = "device-1";           // TODO: use actual device ID
		syncReq["push_clips"] = json::array();       // Empty push for pull-only

		CStringA serverUrlA(m_serverUrl);
		std::string url = serverUrlA.GetString();
		httplib::Client cli(url);
		// Note: httplib unified Client auto-detects HTTPS, cert verification
		// settings not exposed on Client wrapper (use SSLClient directly if needed)
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
			int mergedCount = 0;
			for (const auto& clip : newClips)
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

				// Merge clip into local database
				int newId = MergeRemoteClipToLocal(clip);
				if (newId > 0)
				{
					mergedCount++;
				}
			}

			CString msg;
			msg.Format(_T("PullChanges: received %d clips, %d merged to local DB"), newClips.size(), mergedCount);
			LogMessage(msg);

			// Update last sync time
			m_lastSyncTime = time(nullptr);
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
// ---------------------------------------------------------------------------
BOOL CCloudSyncManager::GetLocalClipsSince(time_t sinceTime, nlohmann::json& clipsArray)
{
	clipsArray = nlohmann::json::array();

	try
	{
		// Query clips modified since last sync
		// For initial sync, get the last 100 clips
		CString csSQL;
		if (sinceTime > 0)
		{
			csSQL.Format(_T("SELECT lID, lDate, mText, CRC, bIsGroup, lParentID, ")
			             _T("clipOrder, stickyClipOrder, lShortCut, globalShortCut, ")
			             _T("lDontAutoDelete, lastPasteDate ")
			             _T("FROM Main WHERE lDate > %lld AND bIsGroup = 0 ")
			             _T("ORDER BY lDate DESC LIMIT 100"), sinceTime);
		}
		else
		{
			// First sync: get recent clips
			csSQL.Format(_T("SELECT lID, lDate, mText, CRC, bIsGroup, lParentID, ")
			             _T("clipOrder, stickyClipOrder, lShortCut, globalShortCut, ")
			             _T("lDontAutoDelete, lastPasteDate ")
			             _T("FROM Main WHERE bIsGroup = 0 ")
			             _T("ORDER BY lDate DESC LIMIT 100"));
		}

		CppSQLite3Query q = theApp.m_db.execQuery(csSQL);

		int clipCount = 0;
		while(q.eof() == false)
		{
			int clipId = q.getIntField(_T("lID"));
			time_t clipDate = (time_t)q.getInt64Field(_T("lDate"));
			CString desc = q.getStringField(_T("mText"));
			DWORD crc = (DWORD)q.getIntField(_T("CRC"));

			// Build clip JSON
			json clipJson;
			clipJson["remote_clip_id"] = std::to_string(clipId);
			clipJson["description"] = CStringToStdString(desc);
			clipJson["crc"] = (int)crc;
			clipJson["is_group"] = false;
			clipJson["clip_order"] = q.getFloatField(_T("clipOrder"));
			clipJson["sticky_order"] = q.getFloatField(_T("stickyClipOrder"));
			clipJson["shortcut"] = q.getIntField(_T("lShortCut"));
			clipJson["global_shortcut"] = q.getIntField(_T("globalShortCut"));
			clipJson["auto_delete"] = q.getIntField(_T("lDontAutoDelete"));
			
			time_t lastPaste = (time_t)q.getInt64Field(_T("lastPasteDate"));
			if (lastPaste > 0)
			{
				clipJson["last_paste_date"] = std::string(ctime(&lastPaste));
			}

			// Load formats for this clip
			json formatsArray;
			if (LoadClipFormats(clipId, formatsArray))
			{
				// Filter HDROP formats (file paths only, no content)
				FilterHDROPForSync(formatsArray);

				// Encrypt formats if encryption is enabled
				if (m_cryptoInitialized)
				{
					if (!EncryptClipFormats(formatsArray))
					{
						CString msg;
						msg.Format(_T("GetLocalClipsSince: encryption failed for clip %d, skipping"), clipId);
						LogMessage(msg);
						q.nextRow();
						continue;
					}
				}

				clipJson["formats"] = formatsArray;
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
				// Encode binary data as base64
				// For simplicity, we'll use a basic base64 encoding
				// In production, use a proper base64 library
				CStringA dataB64;
				
				// Simple approach: store as binary-safe string
				// The server will handle this as base64
				std::string dataStr(reinterpret_cast<const char*>(cData), nDataLen);
				
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
					// For binary formats, use base64-like encoding
					// Use hex encoding for safety
					std::string hexData;
					hexData.reserve(nDataLen * 2);
					const char hex[] = "0123456789ABCDEF";
					for (int i = 0; i < nDataLen; i++)
					{
						hexData += hex[(cData[i] >> 4) & 0x0F];
						hexData += hex[cData[i] & 0x0F];
					}
					formatJson["data"] = hexData;
					formatJson["encoding"] = "hex";
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
// MergeRemoteClipToLocal: Merge a remote clip into local database
// Returns the new/updated clip ID, or -1 on error
// ---------------------------------------------------------------------------
int CCloudSyncManager::MergeRemoteClipToLocal(const nlohmann::json& remoteClip)
{
	try
	{
		// Extract clip metadata
		std::string remoteIdStr = remoteClip.value("remote_clip_id", "");
		std::string desc = remoteClip.value("description", "");
		int crc = remoteClip.value("crc", 0);
		double clipOrder = remoteClip.value("clip_order", 0.0);
		double stickyOrder = remoteClip.value("sticky_order", 0.0);
		int shortcut = remoteClip.value("shortcut", 0);
		int globalShortcut = remoteClip.value("global_shortcut", 0);
		int autoDelete = remoteClip.value("auto_delete", 1);

		// Check if clip already exists (by CRC or remote_clip_id)
		int existingId = -1;
		try
		{
			CString csSQL;
			csSQL.Format(_T("SELECT lID FROM Main WHERE CRC = %d OR mText = '%s' LIMIT 1"),
			             crc, CString(desc.c_str()));
			
			CppSQLite3Query q = theApp.m_db.execQuery(csSQL);
			if (q.eof() == false)
			{
				existingId = q.getIntField(_T("lID"));
			}
		}
		catch (...)
		{
			// Clip doesn't exist, will create new one
		}

		if (existingId > 0)
		{
			// Clip exists - check if we need to update it
			// For now, skip duplicate clips (CRC match)
			CString msg;
			msg.Format(_T("MergeRemoteClipToLocal: duplicate clip (CRC=%d), skipping"), crc);
			LogMessage(msg);
			return existingId;
		}

		// Create new clip
		CClip newClip;
		newClip.m_Desc = CString(desc.c_str());
		newClip.m_CRC = (DWORD)crc;
		newClip.m_parentId = -1;  // Top-level clip
		newClip.m_clipOrder = clipOrder;
		newClip.m_stickyClipOrder = stickyOrder;
		newClip.m_shortCut = shortcut;
		newClip.m_globalShortCut = globalShortcut;
		newClip.m_dontAutoDelete = autoDelete;
		newClip.m_bIsGroup = FALSE;
		newClip.m_Time = CTime::GetCurrentTime();

		// Load formats from JSON
		if (remoteClip.contains("formats") && remoteClip["formats"].is_array())
		{
			for (const auto& formatJson : remoteClip["formats"])
			{
				std::string formatName = formatJson.value("format_name", "");
				std::string dataStr = formatJson.value("data", "");
				int dataSize = formatJson.value("data_size", 0);
				std::string encoding = formatJson.value("encoding", "");

				if (dataStr.empty() || dataSize == 0)
					continue;

				// Decode data based on encoding
				HGLOBAL hGlobal = nullptr;
				
				if (encoding == "hex")
				{
					// Decode hex to binary
					int binaryLen = dataStr.length() / 2;
					hGlobal = GlobalAlloc(GMEM_MOVEABLE, binaryLen);
					if (hGlobal)
					{
						BYTE* pData = (BYTE*)GlobalLock(hGlobal);
						for (int i = 0; i < binaryLen; i++)
						{
							BYTE high = dataStr[i * 2];
							BYTE low = dataStr[i * 2 + 1];
							high = (high >= 'A') ? (high - 'A' + 10) : (high - '0');
							low = (low >= 'A') ? (low - 'A' + 10) : (low - '0');
							pData[i] = (high << 4) | low;
						}
						GlobalUnlock(hGlobal);
					}
				}
				else
				{
					// Plain text (UTF-8)
					hGlobal = GlobalAlloc(GMEM_MOVEABLE, dataSize + 1);
					if (hGlobal)
					{
						char* pData = (char*)GlobalLock(hGlobal);
						memcpy(pData, dataStr.c_str(), dataSize);
						pData[dataSize] = '\0';
						GlobalUnlock(hGlobal);
					}
				}

				if (hGlobal)
				{
					UINT cfType = GetFormatID(CString(formatName.c_str()));
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
				CString msg;
				msg.Format(_T("MergeRemoteClipToLocal: clip merged successfully (ID=%d)"), newClip.m_id);
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
