#pragma once
#include <afx.h>
#include <vector>

// Mock CGetSetOptions for CloudSync_Test
// This is a test stub - the real implementation is in the main Ditto project
// It stores cloud encryption settings in memory for testing purposes

class CGetSetOptions
{
public:
	static CStringA GetCloudEncryptionKey()
	{
		return s_testKey;
	}

	static void SetCloudEncryptionKey(const CString& key)
	{
		s_testKey = CStringA(key);
	}

	static CStringA GetCloudEncryptionSalt()
	{
		return s_testSalt;
	}

	static void SetCloudEncryptionSalt(const CString& salt)
	{
		s_testSalt = CStringA(salt);
	}

	static BOOL GetCloudSyncEncryptionEnabled()
	{
		return s_cloudSyncEnabled;
	}

	static void SetCloudSyncEncryptionEnabled(BOOL enabled)
	{
		s_cloudSyncEnabled = enabled;
	}

	// Clear test state
	static void Reset()
	{
		s_testKey.Empty();
		s_testSalt.Empty();
		s_cloudSyncEnabled = FALSE;
	}

private:
	static CStringA s_testKey;
	static CStringA s_testSalt;
	static BOOL s_cloudSyncEnabled;
};

// Static member definitions
inline CStringA CGetSetOptions::s_testKey;
inline CStringA CGetSetOptions::s_testSalt;
inline BOOL CGetSetOptions::s_cloudSyncEnabled = FALSE;
