#include "stdafx.h"
#include "CloudCrypto.h"
#include <bcrypt.h>
#include <wincrypt.h>
#include <vector>
#include <cstring>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")

// Static member definitions
std::vector<BYTE> CCloudCrypto::m_aesKey;
BOOL CCloudCrypto::m_initialized = FALSE;

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
		OutputDebugStringA("[CloudCrypto] Encrypt: not initialized.\n");
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
		if (ct.empty())
		{
			OutputDebugStringA("[CloudCrypto] Encrypt: AesGcmEncrypt failed.\n");
			return CStringA("");
		}

		// Assemble IV + ciphertext + tag
		std::vector<BYTE> result;
		result.reserve(iv.size() + ct.size() + tag.size());
		result.insert(result.end(), iv.begin(), iv.end());
		result.insert(result.end(), ct.begin(), ct.end());
		result.insert(result.end(), tag.begin(), tag.end());

		return Base64Encode(result);
	}
	catch (...)
	{
		OutputDebugStringA("[CloudCrypto] Encrypt: exception.\n");
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
		OutputDebugStringA("[CloudCrypto] Decrypt: not initialized.\n");
		return CStringA("");
	}

	try
	{
		std::vector<BYTE> data = Base64Decode(encryptedBase64);
		if (data.size() < 12 + 16) // minimum: IV + tag, no ciphertext
		{
			OutputDebugStringA("[CloudCrypto] Decrypt: data too short.\n");
			return CStringA("");
		}

		// Extract IV (first 12 bytes)
		std::vector<BYTE> iv(data.begin(), data.begin() + 12);

		// Extract tag (last 16 bytes)
		std::vector<BYTE> tag(data.end() - 16, data.end());

		// Extract ciphertext (between IV and tag)
		std::vector<BYTE> ct(data.begin() + 12, data.end() - 16);

		// Decrypt
		std::vector<BYTE> pt = AesGcmDecrypt(m_aesKey, iv, ct, tag);
		if (pt.empty() && !ct.empty())
		{
			OutputDebugStringA("[CloudCrypto] Decrypt: AesGcmDecrypt failed.\n");
			return CStringA("");
		}

		return CStringA(reinterpret_cast<const char*>(pt.data()), static_cast<int>(pt.size()));
	}
	catch (...)
	{
		OutputDebugStringA("[CloudCrypto] Decrypt: exception.\n");
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
	result.ReleaseBuffer(dwLen - 1); // trim null terminator
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
		return std::vector<BYTE>();

	// Get key object length
	status = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&cbKeyObject), sizeof(cbKeyObject), &cbData, 0);
	if (!BCRYPT_SUCCESS(status))
	{
		BCryptCloseAlgorithmProvider(hAlg, 0);
		return std::vector<BYTE>();
	}

	// Set chaining mode to GCM
	status = BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
		reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
		sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
	if (!BCRYPT_SUCCESS(status))
	{
		BCryptCloseAlgorithmProvider(hAlg, 0);
		return std::vector<BYTE>();
	}

	// Generate key from raw bytes
	status = BCryptGenerateSymmetricKey(hAlg, &hKey, pbKeyObject, cbKeyObject,
		const_cast<PUCHAR>(key.data()), static_cast<ULONG>(key.size()), 0);
	if (!BCRYPT_SUCCESS(status) || hKey == nullptr)
	{
		BCryptCloseAlgorithmProvider(hAlg, 0);
		return std::vector<BYTE>();
	}

	// Prepare authenticated cipher mode info for GCM
	BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
	BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
	authInfo.pbNonce = const_cast<PUCHAR>(iv.data());
	authInfo.cbNonce = static_cast<ULONG>(iv.size());
	authInfo.pbTag = new BYTE[16];
	authInfo.cbTag = 16;

	// First call to get ciphertext size
	status = BCryptEncrypt(hKey,
		const_cast<PUCHAR>(plaintext.data()), static_cast<ULONG>(plaintext.size()),
		&authInfo, nullptr, 0, nullptr, 0, &cbData, 0);
	if (!BCRYPT_SUCCESS(status))
	{
		delete[] authInfo.pbTag;
		BCryptDestroyKey(hKey);
		BCryptCloseAlgorithmProvider(hAlg, 0);
		return std::vector<BYTE>();
	}

	// Allocate output buffer
	std::vector<BYTE> ciphertext(cbData);
	status = BCryptEncrypt(hKey,
		const_cast<PUCHAR>(plaintext.data()), static_cast<ULONG>(plaintext.size()),
		&authInfo, nullptr, 0,
		ciphertext.data(), static_cast<ULONG>(ciphertext.size()), &cbData, 0);
	if (!BCRYPT_SUCCESS(status))
	{
		delete[] authInfo.pbTag;
		ciphertext.clear();
		BCryptDestroyKey(hKey);
		BCryptCloseAlgorithmProvider(hAlg, 0);
		return ciphertext;
	}
	ciphertext.resize(cbData);

	// Copy out the tag
	outTag.assign(authInfo.pbTag, authInfo.pbTag + 16);
	delete[] authInfo.pbTag;

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
		return std::vector<BYTE>();

	status = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&cbKeyObject), sizeof(cbKeyObject), &cbData, 0);
	if (!BCRYPT_SUCCESS(status))
	{
		BCryptCloseAlgorithmProvider(hAlg, 0);
		return std::vector<BYTE>();
	}

	status = BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
		reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
		sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
	if (!BCRYPT_SUCCESS(status))
	{
		BCryptCloseAlgorithmProvider(hAlg, 0);
		return std::vector<BYTE>();
	}

	status = BCryptGenerateSymmetricKey(hAlg, &hKey, pbKeyObject, cbKeyObject,
		const_cast<PUCHAR>(key.data()), static_cast<ULONG>(key.size()), 0);
	if (!BCRYPT_SUCCESS(status) || hKey == nullptr)
	{
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
		BCryptDestroyKey(hKey);
		BCryptCloseAlgorithmProvider(hAlg, 0);
		return std::vector<BYTE>();
	}

	std::vector<BYTE> plaintext(cbData);
	status = BCryptDecrypt(hKey,
		const_cast<PUCHAR>(ciphertext.data()), static_cast<ULONG>(ciphertext.size()),
		&authInfo, nullptr, 0,
		plaintext.data(), static_cast<ULONG>(plaintext.size()), &cbData, 0);
	if (!BCRYPT_SUCCESS(status))
	{
		plaintext.clear();
		BCryptDestroyKey(hKey);
		BCryptCloseAlgorithmProvider(hAlg, 0);
		return plaintext;
	}
	plaintext.resize(cbData);

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
	if (password.empty() || salt.empty() || iterations <= 0 || dkLen <= 0)
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
