#pragma once
#include <afx.h>
#include <memory>
#include <vector>
#include "../json.hpp"
#include "../httplib.h"

// Custom Windows message for cloud authentication notification
// wParam: HTTP status code (401 = token expired, 403 = forbidden, 998 = encryption salt changed)
// lParam: 0 (reserved)
#ifndef WM_CLOUD_AUTH_REQUIRED
#define WM_CLOUD_AUTH_REQUIRED (WM_USER + 1001)
#endif

// Custom Windows message for deferred ReinitializeSync (avoid UI thread blocking)
#ifndef WM_CLOUD_REINIT_SYNC
#define WM_CLOUD_REINIT_SYNC (WM_USER + 1002)
#endif

// Context for fire-and-forget quick sync threads
struct QuickSyncContext {
	void* pManager;            // CCloudSyncManager*
	LONG* pCounter;            // pointer to active thread counter
	CRITICAL_SECTION* pCS;     // critical section for thread-safe checks
};

class CCloudSyncManager
{
public:
	CCloudSyncManager();
	~CCloudSyncManager();

	// Initialize: read config, start background thread
	BOOL Initialize();

	// Reinitialize: clean stop and full restart (used after encryption re-setup)
	BOOL ReinitializeSync();

	// Stop sync and cleanup thread
	void Stop();

	// Called when a new clip is added locally
	void OnClipAdded(void* pClip);  // void* to avoid including CClip header yet

	// Manual sync trigger
	void TriggerSync();

	// Check if logged in
	BOOL IsLoggedIn() const;

	// Check if encryption is initialized
	BOOL IsEncryptionEnabled() const;

	// Sync status accessors
	CString   GetSyncStatus() const;
	CString   GetLastError() const;
	time_t    GetLastSyncSuccessTime() const;
	BOOL      HasSyncedBefore() const;

	// Mark clips as dont_sync on the server
	void MarkClipsDontSync(const std::vector<int>& localIds);

	// Notify that a group was deleted locally
	void OnGroupDeleted(int localGroupId);

	// Trigger a quick sync (fire-and-forget)
	void TriggerQuickSync();

	// Delete a remote group (used by local delete handler)
	void DeleteRemoteGroup(const std::string& remoteGroupId);

	// Delete remote clips by local ID (notifies server for cross-device sync)
	void DeleteRemoteClips(const std::vector<int>& localIds);

	// One-shot force sync operations
	void ForceDownloadAll();
	void ForceUploadAll();

private:
	// Ensure HTTP client is created/reused for the current server URL
	void EnsureHttpClient();

	// Check if user expects encryption (via registry setting)
	BOOL IsEncryptionExpected();
	CStringA  m_deviceToken;
	CStringA  m_deviceId;      // Device ID from login response
	CString   m_serverUrl;
	HANDLE    m_hStopEvent;
	HANDLE    m_hWsTrigger;    // Signaled by WS thread when clip_added received
	CWinThread* m_pSyncThread;
	CWinThread* m_pWsThread;   // WebSocket listener thread
	BOOL      m_cryptoInitialized;
	time_t    m_lastSyncTime;  // Track last successful sync time
	time_t    m_lastPushTime;  // Track last successful push time (separate from pull sync time)
	CRITICAL_SECTION m_csSync; // Protects m_lastSyncTime and m_cryptoInitialized
	CRITICAL_SECTION m_csHttpClient; // Protects m_httpClient creation/reuse
	LONG      m_nActiveQuickSyncThreads; // Track active quick-push threads
	LONG      m_bFirstPushInProgress;  // 0=idle, 1=first push in progress (InterlockedExchange)
	void*     m_pWsClient;     // httplib::WebSocketClient* (void* to avoid full header)
	int       m_wsReconnectDelay; // Exponential backoff for WS reconnection

	// Encryption retry state
	CWinThread* m_pEncRetryThread;   // Encryption retry thread (when DEK lost at startup)
	HANDLE      m_hEncRetryStop;     // Stop event for retry thread

	// Atomic flags for one-shot force sync operations
	LONG      m_forceOverrideLocal;  // Set before ForceDownload, read&reset in MergeRemoteClipToLocal
	LONG      m_forceOverrideRemote; // Set before ForceUpload, read&reset in QuickSyncThreadProc

	// Reusable HTTP client (httplib::Client) for all REST API calls
	std::unique_ptr<httplib::Client> m_httpClient;
	CString   m_httpClientUrl; // Cached URL to detect server URL changes

	// Critical section for thread-safe access to theApp.m_db from sync thread
	mutable CCriticalSection m_csDb;

	// Thread-safe sync status (protected by m_csStatus)
	CString           m_csSyncStatus;
	CString           m_csLastError;
	time_t            m_lastSyncSuccessTime;
	mutable CRITICAL_SECTION m_csStatus;

	// Background sync thread proc
	static UINT SyncThreadProc(LPVOID pParam);

	// WebSocket listener thread proc
	static UINT WsThreadProc(LPVOID pParam);

	// Quick sync thread proc (fire-and-forget)
	static UINT QuickSyncThreadProc(LPVOID pParam);

	// Force download thread proc (one-shot)
	static UINT ForceSyncThreadProc(LPVOID pParam);

	// Encryption retry thread proc (background retry when DEK lost)
	static UINT EncryptionRetryThreadProc(LPVOID pParam);

	// Start background retry of encryption initialization
	void StartEncryptionRetry();

	// Stop encryption retry thread
	void StopEncryptionRetry();

	// Push local clips to cloud
	BOOL PushNewClips(BOOL bForce = FALSE);

	// Pull changes from cloud
	void PullChanges();

	// Push local groups to cloud (creates/updates groups on server)
	std::vector<std::string> PushGroups();

	// Pull groups from cloud (creates/updates local groups from server)
	void PullGroups();

	// Initialize encryption from stored key
	BOOL InitializeEncryption();

	// Encrypt clip formats before pushing
	BOOL EncryptClipFormats(nlohmann::json& formats);

	// Decrypt clip formats after pulling
	BOOL DecryptClipFormats(nlohmann::json& formats);

	// Extract file paths from CF_HDROP data (returns JSON array of paths)
	static nlohmann::json ExtractFilePathsFromHDROP(const nlohmann::json& hdropFormat);

	// Filter out CF_HDROP formats that contain actual file data (sync paths only)
	static BOOL FilterHDROPForSync(nlohmann::json& formats);

	// Enumerate a single page of local clips modified since sinceTime, bounded by upperBound (0 = no bound)
	BOOL GetLocalClipsSince(time_t sinceTime, time_t upperBound, int offset, int limit, nlohmann::json& clipsArray, bool& hasMore);

	static const int CLOUD_PUSH_BATCH_SIZE;

	// Get the maximum lModifiedDate from the Main table (for first-push baseline snapshot)
	time_t GetMaxLocalClipModifiedDate() const;

	// Load formats for a specific clip
	BOOL LoadClipFormats(int clipId, nlohmann::json& formatsArray);

	// Merge a remote clip into local database (returns new/updated clip ID)
	int MergeRemoteClipToLocal(const nlohmann::json& remoteClip, BOOL bForce = FALSE);

	// Delete a clip from local database (for sync deletions)
	BOOL DeleteLocalClip(int clipId);

	// ---- Remote ID mapping table (M1) ----
	// Ensure the CloudClipMap table exists in the local DB
	void EnsureMappingTable();

	// Save mapping between local and remote clip IDs
	void SaveRemoteIdMapping(int localId, const std::string& remoteId);

	// Look up local clip ID by remote ID; returns -1 if not found
	int GetLocalIdByRemoteId(const std::string& remoteId);

	// Look up remote clip ID by local ID; returns empty string if not found
	std::string GetRemoteIdByLocalId(int localId);

	// ---- Remote group ID mapping table ----
	void EnsureGroupMappingTable();
	void SaveRemoteGroupIdMapping(int localId, const std::string& remoteId);
	int GetLocalGroupIdByRemoteId(const std::string& remoteId);
	std::string GetRemoteGroupIdByLocalId(int localId);
	void DeleteRemoteGroupIdMapping(int localId);
	void DeleteRemoteGroupIdMappingByRemote(const std::string& remoteId);

	// ---- Encryption salt change detection (H3) ----
	// Check if server salt differs from local, notify user if so
	BOOL CheckAndNotifyEncryptionChange();

	// ---- WebSocket (H4) ----
	// Start listening for real-time events from the server
	void StartWebSocket();

	// Stop WebSocket thread and close connection
	void StopWebSocket();

	// Handle incoming WebSocket message (called from WS thread)
	void OnWsMessage(const std::string& msg);

	// Build WebSocket URL from m_serverUrl
	CString BuildWsUrl();
};
