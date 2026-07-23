#pragma once
#include <afx.h>
#include <string>
#include <vector>
#include <mutex>

class CCloudCrypto
{
public:
	static BOOL Initialize(const std::vector<BYTE>& aesKey);

	static void Reset();

	static CStringA Encrypt(const CStringA& plaintext);

	static CStringA Decrypt(const CStringA& encryptedBase64);

	static std::vector<BYTE> DeriveKey(
		const CStringA& password,
		const std::vector<BYTE>& salt,
		int iterations = 100000
	);

	static std::vector<BYTE> RandomBytes(int count);

	static CStringA Base64Encode(const std::vector<BYTE>& data);

	static std::vector<BYTE> Base64Decode(const CStringA& base64);

	static std::vector<BYTE> Sha256(const std::vector<BYTE>& data);

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

	static std::vector<BYTE> PBKDF2(
		const std::vector<BYTE>& password,
		const std::vector<BYTE>& salt,
		int iterations,
		int dkLen
	);

	static std::vector<BYTE> HmacSha256(
		const std::vector<BYTE>& key,
		const std::vector<BYTE>& message
	);

	static CStringA WrapKey(
		const std::vector<BYTE>& kek,
		const std::vector<BYTE>& dek
	);

	static std::vector<BYTE> UnwrapKey(
		const std::vector<BYTE>& kek,
		const CStringA& wrappedBase64
	);

	static CStringA ComputeVerificationHash(
		const CStringA& password,
		const CStringA& saltB64
	);

private:
	static std::vector<BYTE> m_aesKey;
	static std::mutex s_aesMutex;
};