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

struct EncryptionStatusResult
{
	BOOL success;
	CString salt;
	BOOL encryptionEnabled;
	CString passwordHint;
	CString error;
};

struct ChangePasswordResult
{
	BOOL success;
	CString newSalt;
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

	static EncryptionStatusResult GetEncryptionStatus(
		const CString& serverUrl,
		const CString& deviceToken
	);

	static ChangePasswordResult ChangeEncryptionPassword(
		const CString& serverUrl,
		const CString& deviceToken,
		const CString& oldPassword,
		const CString& newPassword
	);

	static EncryptionSetupResult ReVerifyPassword(
		const CString& serverUrl,
		const CString& deviceToken,
		const CString& password
	);

	static BOOL CheckSaltChanged(
		const CString& serverUrl,
		const CString& deviceToken
	);

	static BOOL InitializeCryptoFromStoredKey();

	static CStringA EncryptClipData(const CStringA& plaintext);

	static CStringA DecryptClipData(const CStringA& encryptedBase64);

	static BOOL IsEncryptionReady();

private:
	// Reusable HTTP client for encryption API calls
	static std::unique_ptr<httplib::Client> m_httpClient;
	static CString m_httpClientUrl;
	static CCriticalSection m_csHttpClient;

	// Create or reuse HTTP client for the given server URL and device token
	static void EnsureHttpClient(const CString& serverUrl, const CString& deviceToken);
};