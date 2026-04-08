#pragma once
#include <afx.h>
#include "../json.hpp"

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
	CWinThread* m_pSyncThread;
	BOOL      m_cryptoInitialized;
	time_t    m_lastSyncTime;  // Track last successful sync time
	CRITICAL_SECTION m_csSync; // Protects m_lastSyncTime and m_cryptoInitialized
	LONG      m_nActiveQuickSyncThreads; // Track active quick-push threads

	// Background sync thread proc
	static UINT SyncThreadProc(LPVOID pParam);

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
};
