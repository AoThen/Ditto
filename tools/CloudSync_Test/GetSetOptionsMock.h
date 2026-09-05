#pragma once
#include <afx.h>
#include <vector>

// Mock CGetSetOptions for CloudSync_Test
// This is a test stub - the real implementation is in the main Ditto project
// It stores cloud encryption settings in memory for testing purposes

class CGetSetOptions
{
public:
	static CString GetCloudEncryptionKey()
	{
		return s_testKey;
	}

	static void SetCloudEncryptionKey(LPCTSTR lpszValue)
	{
		s_testKey = lpszValue;
	}

	static CString GetCloudEncryptionSalt()
	{
		return s_testSalt;
	}

	static void SetCloudEncryptionSalt(LPCTSTR lpszValue)
	{
		s_testSalt = lpszValue;
	}

	static BOOL GetCloudSyncEncryptionEnabled()
	{
		return s_cloudSyncEnabled;
	}

	static void SetCloudSyncEncryptionEnabled(BOOL enabled)
	{
		s_cloudSyncEnabled = enabled;
	}

	static CStringA GetCloudDeviceToken()
	{
		return s_deviceToken;
	}

	static void SetCloudDeviceToken(LPCSTR lpszValue)
	{
		s_deviceToken = lpszValue;
	}

	static CStringA GetCloudDeviceId()
	{
		return s_deviceId;
	}

	static void SetCloudDeviceId(LPCSTR lpszValue)
	{
		s_deviceId = lpszValue;
	}

	// Server-url / refresh-token / install-id accessors: DECLARED but not
	// defined here. Their definitions live in TestStubs.cpp, because the test
	// project does not compile Options.cpp. Declaring them here lets tests call
	// them, while CloudAuth.cpp - compiled against the real Options.h - resolves
	// the same symbols from TestStubs.cpp. They must NOT be defined inline here:
	// an inline body is only emitted when a test happens to call it, which would
	// leave CloudAuth.obj unresolved.
	static CString  GetCloudServerUrl();
	static void     SetCloudServerUrl(LPCTSTR lpszValue);
	static CStringA GetCloudRefreshToken();
	static void     SetCloudRefreshToken(LPCSTR lpszValue);
	static CString  GetCloudInstallId();
	static void     SetCloudInstallId(LPCTSTR lpszValue);

	static BOOL GetCloudSyncEnabled()
	{
		return s_cloudSyncEnabled;
	}

	static void SetCloudSyncEnabled(BOOL bValue)
	{
		s_cloudSyncEnabled = bValue;
	}

	static CString GetCloudDeviceName()
	{
		return s_deviceName;
	}

	static void SetCloudDeviceName(LPCTSTR lpszValue)
	{
		s_deviceName = lpszValue;
	}

	static __int64 GetCloudLastSyncTime()
	{
		return s_lastSyncTime;
	}

	static void SetCloudLastSyncTime(__int64 value)
	{
		s_lastSyncTime = value;
	}

	// Clear test state
	static void Reset()
	{
		s_testKey.Empty();
		s_testSalt.Empty();
		s_cloudSyncEnabled = FALSE;
		s_deviceToken.Empty();
		s_deviceId.Empty();
		s_deviceName.Empty();
		s_lastSyncTime = 0;
	}

private:
	static CString s_testKey;
	static CString s_testSalt;
	static BOOL s_cloudSyncEnabled;
	static CStringA s_deviceToken;
	static CStringA s_deviceId;
	static CString s_deviceName;
	static __int64 s_lastSyncTime;
};

// Static member definitions
inline CString CGetSetOptions::s_testKey;
inline CString CGetSetOptions::s_testSalt;
inline BOOL CGetSetOptions::s_cloudSyncEnabled = FALSE;
inline CStringA CGetSetOptions::s_deviceToken;
inline CStringA CGetSetOptions::s_deviceId;
inline CString CGetSetOptions::s_deviceName;
inline __int64 CGetSetOptions::s_lastSyncTime = 0;
