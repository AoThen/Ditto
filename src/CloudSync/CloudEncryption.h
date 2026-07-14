#pragma once
#include <afx.h>
#include <vector>
#include "CloudCrypto.h"

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

	static BOOL CheckSaltChanged(
		const CString& serverUrl,
		const CString& deviceToken
	);

	static BOOL InitializeCryptoFromStoredKey();

	static CStringA EncryptClipData(const CStringA& plaintext);

	static CStringA DecryptClipData(const CStringA& encryptedBase64);

	static BOOL IsEncryptionReady();
};