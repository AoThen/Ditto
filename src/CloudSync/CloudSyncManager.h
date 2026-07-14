#pragma once
#include <afx.h>
#include "../json.hpp"
#include "../httplib.h"

// Custom Windows message for cloud authentication notification
// wParam: HTTP status code (401 = token expired, 403 = forbidden, 998 = encryption salt changed)
// lParam: 0 (reserved)
#ifndef WM_CLOUD_AUTH_REQUIRED
#define WM_CLOUD_AUTH_REQUIRED (WM_USER + 1001)
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

private:
	CStringA  m_deviceToken;
	CStringA  m_deviceId;      // Device ID from login response
	CString   m_serverUrl;
	HANDLE    m_hStopEvent;
	HANDLE    m_hWsTrigger;    // Signaled by WS thread when clip_added received
	CWinThread* m_pSyncThread;
	CWinThread* m_pWsThread;   // WebSocket listener thread
	BOOL      m_cryptoInitialized;
	time_t    m_lastSyncTime;  // Track last successful sync time
	CRITICAL_SECTION m_csSync; // Protects m_lastSyncTime and m_cryptoInitialized
	LONG      m_nActiveQuickSyncThreads; // Track active quick-push threads
	void*     m_pWsClient;     // httplib::WebSocketClient* (void* to avoid full header)
	int       m_wsReconnectDelay; // Exponential backoff for WS reconnection

	// Background sync thread proc
	static UINT SyncThreadProc(LPVOID pParam);

	// WebSocket listener thread proc
	static UINT WsThreadProc(LPVOID pParam);

	// Quick sync thread proc (fire-and-forget)
	static UINT QuickSyncThreadProc(LPVOID pParam);

	// Push local clips to cloud
	void PushNewClips();

	// Pull changes from cloud
	void PullChanges();

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

	// Enumerate local clips modified since lastSyncTime
	BOOL GetLocalClipsSince(time_t sinceTime, nlohmann::json& clipsArray);

	// Load formats for a specific clip
	BOOL LoadClipFormats(int clipId, nlohmann::json& formatsArray);

	// Merge a remote clip into local database (returns new/updated clip ID)
	int MergeRemoteClipToLocal(const nlohmann::json& remoteClip);

	// Delete a clip from local database (for sync deletions)
	BOOL DeleteLocalClip(int clipId);

	// ---- Remote ID mapping table (M1) ----
	// Ensure the CloudClipMap table exists in the local DB
	void EnsureMappingTable();

	// Save mapping between local and remote clip IDs
	void SaveRemoteIdMapping(int localId, const std::string& remoteId);

	// Look up local clip ID by remote ID; returns -1 if not found
	int GetLocalIdByRemoteId(const std::string& remoteId);

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
