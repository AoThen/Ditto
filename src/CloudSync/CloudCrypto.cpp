#include "stdafx.h"
#include "CloudCrypto.h"
#include <bcrypt.h>
#include <wincrypt.h>
#include <vector>
#include <cstring>
#include <cstdarg>

#pragma comment(lib, "bcrypt.lib")

#ifdef _DEBUG
#define CLOUD_CRYPTO_TRACE(fmt, ...) do { CStringA _logMsg; _logMsg.Format(fmt, ##__VA_ARGS__); OutputDebugStringA(_logMsg); } while(0)
#else
#define CLOUD_CRYPTO_TRACE(fmt, ...) ((void)0)
#endif
#pragma comment(lib, "crypt32.lib")

// Static member definitions
std::vector<BYTE> CCloudCrypto::m_aesKey;
BOOL CCloudCrypto::m_initialized = FALSE;

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------
void CCloudCrypto::Reset()
{
	m_aesKey.clear();
	m_initialized = FALSE;
}

// ---------------------------------------------------------------------------
// Initialize
// ---------------------------------------------------------------------------
BOOL CCloudCrypto::Initialize(const std::vector<BYTE>& aesKey)
{
	if (aesKey.size() != 32)
	{
		OutputDebugStringA("[CloudCrypto] Initialize: key must be 32 bytes for AES-256.\n");
		return FALSE;
	}
	try
	{
		m_aesKey = aesKey;
		m_initialized = TRUE;
		return TRUE;
	}
	catch (...)
	{
		OutputDebugStringA("[CloudCrypto] Initialize: exception.\n");
		return FALSE;
	}
}

// ---------------------------------------------------------------------------
// Encrypt: plaintext -> base64(IV[12] + ciphertext + tag[16])
// ---------------------------------------------------------------------------
CStringA CCloudCrypto::Encrypt(const CStringA& plaintext)
{
	if (!m_initialized)
	{
		CLOUD_CRYPTO_TRACE("[CloudCrypto] Encrypt: not initialized.\n");
		return CStringA("");
	}

	try
	{
		// Build plaintext bytes
		std::vector<BYTE> pt(plaintext.GetString(), plaintext.GetString() + plaintext.GetLength());

		// Generate 12-byte IV
		std::vector<BYTE> iv = RandomBytes(12);

		// Encrypt
		std::vector<BYTE> tag;
		std::vector<BYTE> ct = AesGcmEncrypt(m_aesKey, iv, pt, tag);
		if (tag.empty())
		{
			CLOUD_CRYPTO_TRACE("[CloudCrypto] Encrypt: AesGcmEncrypt failed. pt.size()=%zu\n", pt.size());
			return CStringA("");
		}

		// Assemble IV + ciphertext + tag
		std::vector<BYTE> result;
		result.reserve(iv.size() + ct.size() + tag.size());
		result.insert(result.end(), iv.begin(), iv.end());
		result.insert(result.end(), ct.begin(), ct.end());
		result.insert(result.end(), tag.begin(), tag.end());

		CStringA b64 = Base64Encode(result);
		CLOUD_CRYPTO_TRACE("[CloudCrypto] Encrypt: pt.size()=%zu -> ct.size()=%zu, tag.size()=%zu, b64.size()=%d\n", 
			pt.size(), ct.size(), tag.size(), b64.GetLength());
		return b64;
	}
	catch (...)
	{
		CLOUD_CRYPTO_TRACE("[CloudCrypto] Encrypt: exception.\n");
		return CStringA("");
	}
}

// ---------------------------------------------------------------------------
// Decrypt: base64(IV[12] + ciphertext + tag[16]) -> plaintext
// ---------------------------------------------------------------------------
CStringA CCloudCrypto::Decrypt(const CStringA& encryptedBase64)
{
	if (!m_initialized)
	{
		CLOUD_CRYPTO_TRACE("[CloudCrypto] Decrypt: not initialized.\n");
		return CStringA("");
	}

	try
	{
		std::vector<BYTE> data = Base64Decode(encryptedBase64);
		if (data.size() < 12 + 16) // minimum: IV + tag, no ciphertext
		{
			CLOUD_CRYPTO_TRACE("[CloudCrypto] Decrypt: data too short (%zu bytes).\n", data.size());
			return CStringA("");
		}

		// Extract IV (first 12 bytes)
		std::vector<BYTE> iv(data.begin(), data.begin() + 12);

		// Extract tag (last 16 bytes)
		std::vector<BYTE> tag(data.end() - 16, data.end());

		// Extract ciphertext (between IV and tag)
		std::vector<BYTE> ct(data.begin() + 12, data.end() - 16);

		CLOUD_CRYPTO_TRACE("[CloudCrypto] Decrypt: data.size()=%zu, ct.size()=%zu\n", data.size(), ct.size());

		// Decrypt
		std::vector<BYTE> pt = AesGcmDecrypt(m_aesKey, iv, ct, tag);
		CLOUD_CRYPTO_TRACE("[CloudCrypto] Decrypt: pt.size()=%zu\n", pt.size());
		
		if (pt.empty() && !ct.empty())
		{
			CLOUD_CRYPTO_TRACE("[CloudCrypto] Decrypt: AesGcmDecrypt failed (had ct but got empty pt).\n");
			return CStringA("");
		}

		return CStringA(reinterpret_cast<const char*>(pt.data()), static_cast<int>(pt.size()));
	}
	catch (...)
	{
		CLOUD_CRYPTO_TRACE("[CloudCrypto] Decrypt: exception.\n");
		return CStringA("");
	}
}

// ---------------------------------------------------------------------------
// DeriveKey: PBKDF2-HMAC-SHA256
// ---------------------------------------------------------------------------
std::vector<BYTE> CCloudCrypto::DeriveKey(
	const CStringA& password,
	const std::vector<BYTE>& salt,
	int iterations)
{
	std::vector<BYTE> pw(password.GetString(), password.GetString() + password.GetLength());
	return PBKDF2(pw, salt, iterations, 32);
}

// ---------------------------------------------------------------------------
// RandomBytes
// ---------------------------------------------------------------------------
std::vector<BYTE> CCloudCrypto::RandomBytes(int count)
{
	std::vector<BYTE> buf(count);
	if (count <= 0)
		return buf;

	BCRYPT_ALG_HANDLE hAlg = nullptr;
	NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_RNG_ALGORITHM, nullptr, 0);
	if (!BCRYPT_SUCCESS(status) || hAlg == nullptr)
	{
		return buf;
	}

	status = BCryptGenRandom(hAlg, buf.data(), static_cast<ULONG>(count), 0);
	BCryptCloseAlgorithmProvider(hAlg, 0);

	if (!BCRYPT_SUCCESS(status))
	{
		buf.clear();
	}
	return buf;
}

// ---------------------------------------------------------------------------
// Base64Encode
// ---------------------------------------------------------------------------
CStringA CCloudCrypto::Base64Encode(const std::vector<BYTE>& data)
{
	if (data.empty())
		return CStringA("");

	DWORD dwFlags = CRYPT_STRING_NOCRLF | CRYPT_STRING_BASE64;
	DWORD dwLen = 0;
	if (!CryptBinaryToStringA(data.data(), static_cast<DWORD>(data.size()), dwFlags, nullptr, &dwLen))
	{
		return CStringA("");
	}

	CStringA result;
	LPSTR psz = result.GetBuffer(dwLen);
	if (!CryptBinaryToStringA(data.data(), static_cast<DWORD>(data.size()), dwFlags, psz, &dwLen))
	{
		result.ReleaseBuffer(0);
		return CStringA("");
	}
	// On success with a non-NULL buffer, dwLen is the character count WITHOUT
	// the terminating NULL. With CRYPT_STRING_NOCRLF no newline is appended.
	result.ReleaseBuffer(dwLen);
	return result;
}

// ---------------------------------------------------------------------------
// Base64Decode
// ---------------------------------------------------------------------------
std::vector<BYTE> CCloudCrypto::Base64Decode(const CStringA& base64)
{
	if (base64.IsEmpty())
		return std::vector<BYTE>();

	DWORD dwLen = 0;
	if (!CryptStringToBinaryA(base64.GetString(), 0, CRYPT_STRING_BASE64, nullptr, &dwLen, nullptr, nullptr))
	{
		return std::vector<BYTE>();
	}

	std::vector<BYTE> result(dwLen);
	if (!CryptStringToBinaryA(base64.GetString(), 0, CRYPT_STRING_BASE64, result.data(), &dwLen, nullptr, nullptr))
	{
		return std::vector<BYTE>();
	}
	return result;
}

// ---------------------------------------------------------------------------
// AesGcmEncrypt
// ---------------------------------------------------------------------------
std::vector<BYTE> CCloudCrypto::AesGcmEncrypt(
	const std::vector<BYTE>& key,
	const std::vector<BYTE>& iv,
	const std::vector<BYTE>& plaintext,
	std::vector<BYTE>& outTag)
{
	outTag.clear();

	BCRYPT_ALG_HANDLE hAlg = nullptr;
	BCRYPT_KEY_HANDLE hKey = nullptr;
	BYTE pbKeyObject[4096]; // max key object buffer
	DWORD cbKeyObject = 0;
	DWORD cbData = 0;

	NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
	if (!BCRYPT_SUCCESS(status) || hAlg == nullptr)
	{
		CLOUD_CRYPTO_TRACE("[AesGcmEncrypt] BCryptOpenAlgorithmProvider failed: 0x%08X\n", status);
		return std::vector<BYTE>();
	}

	// Get key object length
	status = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&cbKeyObject), sizeof(cbKeyObject), &cbData, 0);
	if (!BCRYPT_SUCCESS(status))
	{
		CLOUD_CRYPTO_TRACE("[AesGcmEncrypt] BCRYPT_OBJECT_LENGTH failed: 0x%08X\n", status);
		BCryptCloseAlgorithmProvider(hAlg, 0);
		return std::vector<BYTE>();
	}

	// Set chaining mode to GCM
	status = BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
		reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
		(static_cast<ULONG>(wcslen(BCRYPT_CHAIN_MODE_GCM)) + 1) * sizeof(wchar_t), 0);
	if (!BCRYPT_SUCCESS(status))
	{
		CLOUD_CRYPTO_TRACE("[AesGcmEncrypt] BCRYPT_CHAINING_MODE failed: 0x%08X\n", status);
		BCryptCloseAlgorithmProvider(hAlg, 0);
		return std::vector<BYTE>();
	}

	// Generate key from raw bytes
	status = BCryptGenerateSymmetricKey(hAlg, &hKey, pbKeyObject, cbKeyObject,
		const_cast<PUCHAR>(key.data()), static_cast<ULONG>(key.size()), 0);
	if (!BCRYPT_SUCCESS(status) || hKey == nullptr)
	{
		CLOUD_CRYPTO_TRACE("[AesGcmEncrypt] BCryptGenerateSymmetricKey failed: 0x%08X\n", status);
		BCryptCloseAlgorithmProvider(hAlg, 0);
		return std::vector<BYTE>();
	}

	// Prepare authenticated cipher mode info for GCM.
	// For GCM, the authentication tag is written to a caller-supplied pbTag
	// buffer (NOT appended to the ciphertext output). The ciphertext output
	// has the same length as the plaintext.
	std::vector<BYTE> tagBuf(16); // AES-GCM tag is 16 bytes
	BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
	BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
	authInfo.pbNonce = const_cast<PUCHAR>(iv.data());
	authInfo.cbNonce = static_cast<ULONG>(iv.size());
	authInfo.pbTag = tagBuf.data();
	authInfo.cbTag = static_cast<ULONG>(tagBuf.size());

	// First call to get ciphertext size (equals plaintext size for GCM)
	status = BCryptEncrypt(hKey,
		const_cast<PUCHAR>(plaintext.data()), static_cast<ULONG>(plaintext.size()),
		&authInfo, nullptr, 0, nullptr, 0, &cbData, 0);
	if (!BCRYPT_SUCCESS(status))
	{
		CLOUD_CRYPTO_TRACE("[AesGcmEncrypt] BCryptEncrypt(size) failed: 0x%08X, pt.size=%zu\n", status, plaintext.size());
		BCryptDestroyKey(hKey);
		BCryptCloseAlgorithmProvider(hAlg, 0);
		return std::vector<BYTE>();
	}

	// Allocate output buffer for the ciphertext (tag goes to tagBuf separately)
	std::vector<BYTE> ciphertext(cbData);
	status = BCryptEncrypt(hKey,
		const_cast<PUCHAR>(plaintext.data()), static_cast<ULONG>(plaintext.size()),
		&authInfo, nullptr, 0,
		ciphertext.empty() ? nullptr : ciphertext.data(), static_cast<ULONG>(ciphertext.size()), &cbData, 0);
	if (!BCRYPT_SUCCESS(status))
	{
		CLOUD_CRYPTO_TRACE("[AesGcmEncrypt] BCryptEncrypt(actual) failed: 0x%08X\n", status);
		BCryptDestroyKey(hKey);
		BCryptCloseAlgorithmProvider(hAlg, 0);
		return std::vector<BYTE>();
	}
	ciphertext.resize(cbData);

	// The authentication tag was written to tagBuf by BCryptEncrypt.
	outTag = tagBuf;

	BCryptDestroyKey(hKey);
	BCryptCloseAlgorithmProvider(hAlg, 0);

	return ciphertext;
}

// ---------------------------------------------------------------------------
// AesGcmDecrypt
// ---------------------------------------------------------------------------
std::vector<BYTE> CCloudCrypto::AesGcmDecrypt(
	const std::vector<BYTE>& key,
	const std::vector<BYTE>& iv,
	const std::vector<BYTE>& ciphertext,
	const std::vector<BYTE>& tag)
{
	BCRYPT_ALG_HANDLE hAlg = nullptr;
	BCRYPT_KEY_HANDLE hKey = nullptr;
	BYTE pbKeyObject[4096];
	DWORD cbKeyObject = 0;
	DWORD cbData = 0;

	NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
	if (!BCRYPT_SUCCESS(status) || hAlg == nullptr)
	{
		CLOUD_CRYPTO_TRACE("[AesGcmDecrypt] BCryptOpenAlgorithmProvider failed: 0x%08X\n", status);
		return std::vector<BYTE>();
	}

	status = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&cbKeyObject), sizeof(cbKeyObject), &cbData, 0);
	if (!BCRYPT_SUCCESS(status))
	{
		CLOUD_CRYPTO_TRACE("[AesGcmDecrypt] BCRYPT_OBJECT_LENGTH failed: 0x%08X\n", status);
		BCryptCloseAlgorithmProvider(hAlg, 0);
		return std::vector<BYTE>();
	}

	status = BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
		reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
		(static_cast<ULONG>(wcslen(BCRYPT_CHAIN_MODE_GCM)) + 1) * sizeof(wchar_t), 0);
	if (!BCRYPT_SUCCESS(status))
	{
		CLOUD_CRYPTO_TRACE("[AesGcmDecrypt] BCRYPT_CHAINING_MODE failed: 0x%08X\n", status);
		BCryptCloseAlgorithmProvider(hAlg, 0);
		return std::vector<BYTE>();
	}

	status = BCryptGenerateSymmetricKey(hAlg, &hKey, pbKeyObject, cbKeyObject,
		const_cast<PUCHAR>(key.data()), static_cast<ULONG>(key.size()), 0);
	if (!BCRYPT_SUCCESS(status) || hKey == nullptr)
	{
		CLOUD_CRYPTO_TRACE("[AesGcmDecrypt] BCryptGenerateSymmetricKey failed: 0x%08X\n", status);
		BCryptCloseAlgorithmProvider(hAlg, 0);
		return std::vector<BYTE>();
	}

	BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
	BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
	authInfo.pbNonce = const_cast<PUCHAR>(iv.data());
	authInfo.cbNonce = static_cast<ULONG>(iv.size());

	// Copy tag to a mutable buffer
	std::vector<BYTE> tagBuf(tag.begin(), tag.end());
	authInfo.pbTag = tagBuf.data();
	authInfo.cbTag = static_cast<ULONG>(tagBuf.size());

	// First call to get plaintext size
	status = BCryptDecrypt(hKey,
		const_cast<PUCHAR>(ciphertext.data()), static_cast<ULONG>(ciphertext.size()),
		&authInfo, nullptr, 0, nullptr, 0, &cbData, 0);
	if (!BCRYPT_SUCCESS(status))
	{
		CLOUD_CRYPTO_TRACE("[AesGcmDecrypt] BCryptDecrypt(size) failed: status=0x%08X, ct.size=%zu\n", status, ciphertext.size());
		BCryptDestroyKey(hKey);
		BCryptCloseAlgorithmProvider(hAlg, 0);
		return std::vector<BYTE>();
	}

	CLOUD_CRYPTO_TRACE("[AesGcmDecrypt] BCryptDecrypt(size) ok: cbData=%lu\n", cbData);

	std::vector<BYTE> plaintext(cbData);
	status = BCryptDecrypt(hKey,
		const_cast<PUCHAR>(ciphertext.data()), static_cast<ULONG>(ciphertext.size()),
		&authInfo, nullptr, 0,
		plaintext.data(), static_cast<ULONG>(plaintext.size()), &cbData, 0);
	if (!BCRYPT_SUCCESS(status))
	{
		CLOUD_CRYPTO_TRACE("[AesGcmDecrypt] BCryptDecrypt(actual) failed: status=0x%08X\n", status);
		plaintext.clear();
		BCryptDestroyKey(hKey);
		BCryptCloseAlgorithmProvider(hAlg, 0);
		return plaintext;
	}
	plaintext.resize(cbData);
	CLOUD_CRYPTO_TRACE("[AesGcmDecrypt] BCryptDecrypt(actual) ok: pt.size=%zu\n", plaintext.size());

	BCryptDestroyKey(hKey);
	BCryptCloseAlgorithmProvider(hAlg, 0);

	return plaintext;
}

// ---------------------------------------------------------------------------
// PBKDF2-HMAC-SHA256
// ---------------------------------------------------------------------------
std::vector<BYTE> CCloudCrypto::PBKDF2(
	const std::vector<BYTE>& password,
	const std::vector<BYTE>& salt,
	int iterations,
	int dkLen)
{
	if (salt.empty() || iterations <= 0 || dkLen <= 0)
		return std::vector<BYTE>();

	// hLen = 32 (SHA-256 output size)
	const int hLen = 32;
	int l = (dkLen + hLen - 1) / hLen; // ceil(dkLen / hLen)

	std::vector<BYTE> dk;
	dk.reserve(dkLen);

	for (int i = 1; i <= l; i++)
	{
		// Build salt || INT_32_BE(i)
		std::vector<BYTE> saltBlock = salt;
		saltBlock.push_back(static_cast<BYTE>((i >> 24) & 0xFF));
		saltBlock.push_back(static_cast<BYTE>((i >> 16) & 0xFF));
		saltBlock.push_back(static_cast<BYTE>((i >> 8) & 0xFF));
		saltBlock.push_back(static_cast<BYTE>(i & 0xFF));

		// U_1 = HMAC-SHA256(password, salt || INT_32_BE(i))
		std::vector<BYTE> u = HmacSha256(password, saltBlock);
		std::vector<BYTE> t = u; // T = U_1

		for (int c = 1; c < iterations; c++)
		{
			// U_c = HMAC-SHA256(password, U_{c-1})
			u = HmacSha256(password, u);

			// T = T XOR U_c
			for (int j = 0; j < hLen; j++)
			{
				t[j] ^= u[j];
			}
		}

		dk.insert(dk.end(), t.begin(), t.end());
	}

	// Truncate to dkLen
	dk.resize(dkLen);
	return dk;
}

// ---------------------------------------------------------------------------
// HmacSha256: RFC 2104
// ---------------------------------------------------------------------------
std::vector<BYTE> CCloudCrypto::HmacSha256(
	const std::vector<BYTE>& key,
	const std::vector<BYTE>& message)
{
	const int blockSize = 64; // SHA-256 block size
	std::vector<BYTE> actualKey = key;

	// If key is longer than block size, hash it
	if (actualKey.size() > blockSize)
	{
		actualKey = Sha256(actualKey);
	}

	// Pad key to block size
	actualKey.resize(blockSize, 0);

	// Create inner and outer padded keys
	std::vector<BYTE> innerKey(blockSize);
	std::vector<BYTE> outerKey(blockSize);
	for (int i = 0; i < blockSize; i++)
	{
		innerKey[i] = actualKey[i] ^ 0x36;
		outerKey[i] = actualKey[i] ^ 0x5c;
	}

	// Inner hash: SHA256(innerKey || message)
	std::vector<BYTE> innerData;
	innerData.reserve(innerKey.size() + message.size());
	innerData.insert(innerData.end(), innerKey.begin(), innerKey.end());
	innerData.insert(innerData.end(), message.begin(), message.end());
	std::vector<BYTE> innerHash = Sha256(innerData);

	// Outer hash: SHA256(outerKey || innerHash)
	std::vector<BYTE> outerData;
	outerData.reserve(outerKey.size() + innerHash.size());
	outerData.insert(outerData.end(), outerKey.begin(), outerKey.end());
	outerData.insert(outerData.end(), innerHash.begin(), innerHash.end());
	return Sha256(outerData);
}

// ---------------------------------------------------------------------------
// Sha256 using Windows CNG
// ---------------------------------------------------------------------------
std::vector<BYTE> CCloudCrypto::Sha256(const std::vector<BYTE>& data)
{
	if (data.empty())
		return std::vector<BYTE>();

	BCRYPT_ALG_HANDLE hAlg = nullptr;
	NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
	if (!BCRYPT_SUCCESS(status) || hAlg == nullptr)
		return std::vector<BYTE>();

	DWORD cbHashObject = 0;
	DWORD cbData = 0;
	status = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&cbHashObject), sizeof(cbHashObject), &cbData, 0);
	if (!BCRYPT_SUCCESS(status))
	{
		BCryptCloseAlgorithmProvider(hAlg, 0);
		return std::vector<BYTE>();
	}

	DWORD hashLen = 0;
	status = BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLen), sizeof(hashLen), &cbData, 0);
	if (!BCRYPT_SUCCESS(status))
	{
		BCryptCloseAlgorithmProvider(hAlg, 0);
		return std::vector<BYTE>();
	}

	std::vector<BYTE> hashObject(cbHashObject);
	std::vector<BYTE> hashResult(hashLen);

	BCRYPT_HASH_HANDLE hHash = nullptr;
	status = BCryptCreateHash(hAlg, &hHash, hashObject.data(), cbHashObject, nullptr, 0, 0);
	if (!BCRYPT_SUCCESS(status) || hHash == nullptr)
	{
		BCryptCloseAlgorithmProvider(hAlg, 0);
		return std::vector<BYTE>();
	}

	status = BCryptHashData(hHash, const_cast<PUCHAR>(data.data()), static_cast<ULONG>(data.size()), 0);
	if (!BCRYPT_SUCCESS(status))
	{
		BCryptDestroyHash(hHash);
		BCryptCloseAlgorithmProvider(hAlg, 0);
		return std::vector<BYTE>();
	}

	status = BCryptFinishHash(hHash, hashResult.data(), static_cast<ULONG>(hashResult.size()), 0);
	BCryptDestroyHash(hHash);
	BCryptCloseAlgorithmProvider(hAlg, 0);

	if (!BCRYPT_SUCCESS(status))
		return std::vector<BYTE>();

	return hashResult;
}

// ---------------------------------------------------------------------------
// WrapKey: AES-GCM encrypt a DEK with a KEK
// Returns base64(IV[12] + ciphertext[32] + tag[16])
// ---------------------------------------------------------------------------
CStringA CCloudCrypto::WrapKey(
	const std::vector<BYTE>& kek,
	const std::vector<BYTE>& dek)
{
	if (kek.size() != 32 || dek.size() != 32)
	{
		OutputDebugStringA("[CloudCrypto] WrapKey: KEK and DEK must be 32 bytes.\n");
		return CStringA("");
	}

	std::vector<BYTE> iv = RandomBytes(12);
	std::vector<BYTE> tag;
	std::vector<BYTE> ct = AesGcmEncrypt(kek, iv, dek, tag);
	if (ct.empty())
	{
		OutputDebugStringA("[CloudCrypto] WrapKey: AesGcmEncrypt failed.\n");
		return CStringA("");
	}

	std::vector<BYTE> result;
	result.reserve(iv.size() + ct.size() + tag.size());
	result.insert(result.end(), iv.begin(), iv.end());
	result.insert(result.end(), ct.begin(), ct.end());
	result.insert(result.end(), tag.begin(), tag.end());

	return Base64Encode(result);
}

// ---------------------------------------------------------------------------
// UnwrapKey: AES-GCM decrypt a wrapped DEK with a KEK
// Input: base64(IV[12] + ciphertext + tag[16])
// Returns: raw DEK (32 bytes), or empty on failure
// ---------------------------------------------------------------------------
std::vector<BYTE> CCloudCrypto::UnwrapKey(
	const std::vector<BYTE>& kek,
	const CStringA& wrappedBase64)
{
	if (kek.size() != 32)
	{
		OutputDebugStringA("[CloudCrypto] UnwrapKey: KEK must be 32 bytes.\n");
		return std::vector<BYTE>();
	}

	std::vector<BYTE> data = Base64Decode(wrappedBase64);
	if (data.size() < 12 + 16)
	{
		OutputDebugStringA("[CloudCrypto] UnwrapKey: data too short.\n");
		return std::vector<BYTE>();
	}

	std::vector<BYTE> iv(data.begin(), data.begin() + 12);
	std::vector<BYTE> tag(data.end() - 16, data.end());
	std::vector<BYTE> ct(data.begin() + 12, data.end() - 16);

	std::vector<BYTE> dek = AesGcmDecrypt(kek, iv, ct, tag);
	if (dek.empty())
	{
		OutputDebugStringA("[CloudCrypto] UnwrapKey: AesGcmDecrypt failed (wrong KEK?).\n");
		return std::vector<BYTE>();
	}

	return dek;
}

// ---------------------------------------------------------------------------
// ComputeVerificationHash: SHA256("DITTO_ENC_AUTH_v1:" + password + ":" + saltB64)
// Returns base64 of hash
// ---------------------------------------------------------------------------
CStringA CCloudCrypto::ComputeVerificationHash(
	const CStringA& password,
	const CStringA& saltB64)
{
	const char* domainTag = "DITTO_ENC_AUTH_v1:";
	CStringA input = CStringA(domainTag) + password + ":" + saltB64;

	std::vector<BYTE> inputBytes(
		input.GetString(),
		input.GetString() + input.GetLength());

	std::vector<BYTE> hash = Sha256(inputBytes);
	if (hash.empty())
		return CStringA("");

	return Base64Encode(hash);
}
