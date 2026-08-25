#pragma once
#include <afx.h>
#include <vector>

// .dittokey file format for cross-device key transfer
// Format: JSON with base64-encoded key + metadata
// Structure:
// {
//   "version": 1,
//   "username": "user@example.com",
//   "created_at": "2025-01-01T00:00:00Z",
//   "salt": "<base64>",
//   "encrypted_key": "<base64>",  // AES key encrypted with user password
//   "checksum": "<base64>"        // SHA-256 of original key for verification
// }

struct DittoKeyData
{
	int version;
	CString username;
	CString createdAt;
	CString salt;         // base64
	CString encryptedKey; // base64
	CString checksum;     // base64
};

class CCloudKeyExport
{
public:
	// Export current encryption key to .dittokey file
	// Requires the user's password to encrypt the key
	// psError (optional) receives a human-readable failure reason
	static BOOL ExportKey(
		const CString& filePath,
		const CString& username,
		const CString& password,
		CString* psError = nullptr
	);

	// Import key from .dittokey file
	// Requires the user's password to decrypt the key
	// psError (optional) receives a human-readable failure reason
	static BOOL ImportKey(
		const CString& filePath,
		const CString& password,
		DittoKeyData& outKeyData,
		CString* psError = nullptr
	);

	// Initialize crypto from imported key data
	static BOOL InitializeFromImportedKey(const DittoKeyData& keyData);

	// Verify .dittokey file format (returns true if valid)
	static BOOL IsValidKeyFile(const CString& filePath);

	// Get key file info (username, created_at, etc.)
	static BOOL GetKeyFileInfo(const CString& filePath, DittoKeyData& outInfo);
};
