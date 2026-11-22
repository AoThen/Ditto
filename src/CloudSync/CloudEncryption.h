#pragma once
#include <afx.h>
#include <memory>
#include <vector>
#include "CloudCrypto.h"

namespace httplib { class Client; }

struct EncryptionSetupResult
{
	BOOL success;
	CString salt;
	BOOL encryptionEnabled;
	CString error;
};

class CCloudEncryption
{
public:
	static EncryptionSetupResult SetupEncryption(
		const CString& serverUrl,
		const CString& deviceToken,
		const CString& password
	);

	static EncryptionSetupResult ReVerifyPassword(
		const CString& serverUrl,
		const CString& deviceToken,
		const CString& password
	);

	static CStringA EncryptClipData(const CStringA& plaintext);

	static CStringA DecryptClipData(const CStringA& encryptedBase64);

	static BOOL IsEncryptionReady();

private:
	// Reusable HTTP client for encryption API calls.
	// Concurrency note: these methods are only called from the UI thread
	// (options page). m_csHttpClient serializes client creation/reset only;
	// do NOT call the request methods from background threads without adding
	// proper locking first.
	static std::unique_ptr<httplib::Client> m_httpClient;
	static CString m_httpClientUrl;
	static CCriticalSection m_csHttpClient;
	static CStringA m_lastHttpToken;

	// Create or reuse HTTP client for the given server URL and device token
	static void EnsureHttpClient(const CString& serverUrl, const CString& deviceToken);
};