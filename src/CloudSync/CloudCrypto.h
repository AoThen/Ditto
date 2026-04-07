#pragma once
#include <afx.h>
#include <string>
#include <vector>

// Cloud end-to-end encryption using AES-256-GCM
// Different from Ditto's LAN sync encryption (AES-256-CBC)
class CCloudCrypto
{
public:
	// Initialize with AES key (32 bytes)
	// The key is derived from user password via PBKDF2 (done externally)
	static BOOL Initialize(const std::vector<BYTE>& aesKey);

	// Encrypt plaintext -> returns base64(gcm_ciphertext + iv + tag)
	static CStringA Encrypt(const CStringA& plaintext);

	// Decrypt base64(gcm_ciphertext + iv + tag) -> returns plaintext
	static CStringA Decrypt(const CStringA& encryptedBase64);

	// Derive AES key from password using PBKDF2-HMAC-SHA256
	// Returns 32-byte key
	static std::vector<BYTE> DeriveKey(
		const CStringA& password,
		const std::vector<BYTE>& salt,  // 32 bytes from server
		int iterations = 100000
	);

	// Generate random bytes (for IV, salt, etc.)
	static std::vector<BYTE> RandomBytes(int count);

	// Base64 encode
	static CStringA Base64Encode(const std::vector<BYTE>& data);

	// Base64 decode
	static std::vector<BYTE> Base64Decode(const CStringA& base64);

	// Base64 decode (CString overload)
	static std::vector<BYTE> Base64Decode(const CString& base64);

	// SHA-256 hash (needed by CloudKeyExport)
	static std::vector<BYTE> Sha256(const std::vector<BYTE>& data);

	// AES-256-GCM encrypt/decrypt (needed by CloudKeyExport)
	static std::vector<BYTE> AesGcmEncrypt(
		const std::vector<BYTE>& key,
		const std::vector<BYTE>& iv,
		const std::vector<BYTE>& plaintext,
		std::vector<BYTE>& outTag
	);

	static std::vector<BYTE> AesGcmDecrypt(
		const std::vector<BYTE>& key,
		const std::vector<BYTE>& iv,
		const std::vector<BYTE>& ciphertext,
		const std::vector<BYTE>& tag
	);

private:
	// AES-256-GCM key (stored after Initialize)
	static std::vector<BYTE> m_aesKey;
	static BOOL m_initialized;

	// PBKDF2-HMAC-SHA256 using Windows CNG API
	static std::vector<BYTE> PBKDF2(
		const std::vector<BYTE>& password,
		const std::vector<BYTE>& salt,
		int iterations,
		int dkLen
	);

	// HMAC-SHA256 helper
	static std::vector<BYTE> HmacSha256(
		const std::vector<BYTE>& key,
		const std::vector<BYTE>& message
	);
};
