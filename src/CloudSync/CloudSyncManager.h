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
	CString   m_serverUrl;
	HANDLE    m_hStopEvent;
	CWinThread* m_pSyncThread;
	BOOL      m_cryptoInitialized;

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
};
