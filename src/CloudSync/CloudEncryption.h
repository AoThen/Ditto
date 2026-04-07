#pragma once
#include <afx.h>
#include <vector>
#include "CloudCrypto.h"

struct EncryptionSetupResult
{
	BOOL success;
	CString salt;         // base64 salt from server
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

class CCloudEncryption
{
public:
	// Setup encryption: fetch salt from server, derive key, initialize crypto
	static EncryptionSetupResult SetupEncryption(
		const CString& serverUrl,
		const CString& deviceToken,
		const CString& password
	);

	// Get encryption status from server
	static EncryptionStatusResult GetEncryptionStatus(
		const CString& serverUrl,
		const CString& deviceToken
	);

	// Initialize crypto from stored key (called at startup)
	static BOOL InitializeCryptoFromStoredKey();

	// Encrypt clip data before push
	static CStringA EncryptClipData(const CStringA& plaintext);

	// Decrypt clip data after pull
	static CStringA DecryptClipData(const CStringA& encryptedBase64);

	// Check if encryption is initialized locally
	static BOOL IsEncryptionReady();
};
