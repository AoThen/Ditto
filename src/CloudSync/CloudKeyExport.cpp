#include "stdafx.h"
#include "CloudKeyExport.h"
#include "CloudCrypto.h"
#include "../httplib.h"
#include "../json.hpp"

// Use mock CGetSetOptions for tests, real implementation for main project
#ifdef CLOUDSYNC_TEST
#include "GetSetOptionsMock.h"
#else
#include "../Options.h"
#endif

using json = nlohmann::json;

static std::string CStringToStdString(const CString& str)
{
	if (str.IsEmpty())
		return std::string();
	CT2A utf8(str, CP_UTF8);
	if (utf8.m_psz == nullptr)
		return std::string();
	return std::string(utf8.m_psz);
}

static CString StdStringToCString(const std::string& str)
{
	if (str.empty())
		return CString();
	CA2W wide(str.c_str(), CP_UTF8);
	return CString(wide);
}

// ---------------------------------------------------------------------------
// ExportKey: export current encryption key to .dittokey file
// ---------------------------------------------------------------------------
BOOL CCloudKeyExport::ExportKey(
	const CString& filePath,
	const CString& username,
	const CString& password)
{
	try
	{
		// Get current encryption key from settings
		CString csKeyB64 = CGetSetOptions::GetCloudEncryptionKey();
		CT2A keyB64A(csKeyB64, CP_UTF8);
		CStringA keyBase64(keyB64A);
		CString csSaltB64 = CGetSetOptions::GetCloudEncryptionSalt();
		CT2A saltB64A(csSaltB64, CP_UTF8);
		CStringA saltBase64(saltB64A);

		if (keyBase64.IsEmpty() || saltBase64.IsEmpty())
		{
			OutputDebugStringA("[KeyExport] No encryption key found.\n");
			return FALSE;
		}

		// Decode key
		std::vector<BYTE> keyBytes = CCloudCrypto::Base64Decode(keyBase64);
		if (keyBytes.size() != 32)
		{
			OutputDebugStringA("[KeyExport] Invalid key size.\n");
			return FALSE;
		}

		// Compute checksum (SHA-256 of original key)
		std::vector<BYTE> checksum = CCloudCrypto::Sha256(keyBytes);
		CStringA checksumB64 = CCloudCrypto::Base64Encode(checksum);

		// Derive a key from password + salt for encrypting the exported key
		std::vector<BYTE> saltBytes = CCloudCrypto::Base64Decode(saltBase64);
		std::vector<BYTE> exportKey = CCloudCrypto::DeriveKey(
			CStringA(password), saltBytes, 100000);

		// Encrypt the AES key with the password-derived key
		// Use a random IV for each export
		std::vector<BYTE> iv = CCloudCrypto::RandomBytes(12);
		std::vector<BYTE> tag;
		std::vector<BYTE> encryptedKey = CCloudCrypto::AesGcmEncrypt(
			exportKey, iv, keyBytes, tag);

		// Assemble IV + encrypted key + tag
		std::vector<BYTE> encryptedPayload;
		encryptedPayload.reserve(iv.size() + encryptedKey.size() + tag.size());
		encryptedPayload.insert(encryptedPayload.end(), iv.begin(), iv.end());
		encryptedPayload.insert(encryptedPayload.end(), encryptedKey.begin(), encryptedKey.end());
		encryptedPayload.insert(encryptedPayload.end(), tag.begin(), tag.end());

		CStringA encryptedPayloadB64 = CCloudCrypto::Base64Encode(encryptedPayload);

		// Get current timestamp
		CTime now = CTime::GetCurrentTime();
		CString timestamp = now.Format(_T("%Y-%m-%dT%H:%M:%SZ"));

		// Build JSON
		json keyJson;
		keyJson["version"] = 1;
		keyJson["username"] = CStringToStdString(username);
		keyJson["created_at"] = CStringToStdString(timestamp);
		keyJson["salt"] = saltBase64.GetString();
		keyJson["encrypted_key"] = encryptedPayloadB64.GetString();
		keyJson["checksum"] = checksumB64.GetString();

		std::string keyStr = keyJson.dump(2);

		// Write to file
		CStringA keyStrA(keyStr.c_str());
		CFile file;
		if (!file.Open(filePath, CFile::modeCreate | CFile::modeWrite | CFile::typeBinary))
		{
			OutputDebugString(_T("[KeyExport] Failed to open file for writing.\n"));
			return FALSE;
		}

		file.Write(keyStrA.GetString(), keyStrA.GetLength());
		file.Close();

		OutputDebugString(_T("[KeyExport] Key exported successfully.\n"));
		return TRUE;
	}
	catch (const std::exception& e)
	{
		CString err;
		err.Format(_T("[KeyExport] Export error: %hs"), e.what());
		OutputDebugString(err);
		return FALSE;
	}
	catch (...)
	{
		OutputDebugString(_T("[KeyExport] Export exception.\n"));
		return FALSE;
	}
}

// ---------------------------------------------------------------------------
// ImportKey: import key from .dittokey file
// ---------------------------------------------------------------------------
BOOL CCloudKeyExport::ImportKey(
	const CString& filePath,
	const CString& password,
	DittoKeyData& outKeyData)
{
	try
	{
		// Read file
		CFile file;
		if (!file.Open(filePath, CFile::modeRead | CFile::typeBinary))
		{
			OutputDebugString(_T("[KeyExport] Failed to open key file.\n"));
			return FALSE;
		}

		ULONGLONG fileSize = file.GetLength();
		if (fileSize > 64 * 1024) // Max 64KB
		{
			OutputDebugString(_T("[KeyExport] Key file too large.\n"));
			file.Close();
			return FALSE;
		}

		CStringA fileContent;
		LPSTR buf = fileContent.GetBuffer(static_cast<int>(fileSize) + 1);
		UINT bytesRead = file.Read(buf, static_cast<UINT>(fileSize));
		fileContent.ReleaseBuffer(bytesRead);
		file.Close();

		// Parse JSON
		json keyJson = json::parse(fileContent.GetString());

		// Validate version
		if (!keyJson.contains("version") || keyJson["version"].get<int>() != 1)
		{
			OutputDebugStringA("[KeyExport] Unsupported key file version.\n");
			return FALSE;
		}

		// Extract fields
		outKeyData.version = keyJson["version"].get<int>();
		outKeyData.username = StdStringToCString(keyJson["username"].get<std::string>());
		outKeyData.createdAt = StdStringToCString(keyJson["created_at"].get<std::string>());
		outKeyData.salt = StdStringToCString(keyJson["salt"].get<std::string>());
		outKeyData.encryptedKey = StdStringToCString(keyJson["encrypted_key"].get<std::string>());
		outKeyData.checksum = StdStringToCString(keyJson["checksum"].get<std::string>());

		// Decrypt the key
		std::vector<BYTE> encryptedPayload = CCloudCrypto::Base64Decode(CStringA(outKeyData.encryptedKey));
		if (encryptedPayload.size() < 12 + 16)
		{
			OutputDebugStringA("[KeyExport] Encrypted key too short.\n");
			return FALSE;
		}

		// Extract IV, ciphertext, tag
		std::vector<BYTE> iv(encryptedPayload.begin(), encryptedPayload.begin() + 12);
		std::vector<BYTE> tag(encryptedPayload.end() - 16, encryptedPayload.end());
		std::vector<BYTE> ciphertext(encryptedPayload.begin() + 12, encryptedPayload.end() - 16);

		// Derive decryption key from password + salt
		std::vector<BYTE> saltBytes = CCloudCrypto::Base64Decode(CStringA(outKeyData.salt));
		std::vector<BYTE> exportKey = CCloudCrypto::DeriveKey(
			CStringA(password), saltBytes, 100000);

		// Decrypt
		std::vector<BYTE> decryptedKey = CCloudCrypto::AesGcmDecrypt(
			exportKey, iv, ciphertext, tag);
		if (decryptedKey.empty())
		{
			OutputDebugStringA("[KeyExport] Decryption failed (wrong password?).\n");
			return FALSE;
		}

		// Verify checksum
		std::vector<BYTE> expectedChecksum = CCloudCrypto::Base64Decode(CStringA(outKeyData.checksum));
		std::vector<BYTE> actualChecksum = CCloudCrypto::Sha256(decryptedKey);
		if (expectedChecksum != actualChecksum)
		{
			OutputDebugStringA("[KeyExport] Checksum mismatch (key corrupted).\n");
			return FALSE;
		}

		// Store decrypted key
		CStringA keyBase64 = CCloudCrypto::Base64Encode(decryptedKey);
		CGetSetOptions::SetCloudEncryptionKey(CString(keyBase64));
		CGetSetOptions::SetCloudEncryptionSalt(CString(outKeyData.salt));
		CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);

		// Initialize crypto
		return CCloudCrypto::Initialize(decryptedKey);
	}
	catch (const std::exception& e)
	{
		CString err;
		err.Format(_T("[KeyExport] Import error: %hs"), e.what());
		OutputDebugString(err);
		return FALSE;
	}
	catch (...)
	{
		OutputDebugString(_T("[KeyExport] Import exception.\n"));
		return FALSE;
	}
}

// ---------------------------------------------------------------------------
// InitializeFromImportedKey: initialize crypto from already-imported key data
// Reads the stored key from registry (set by ImportKey) and initializes crypto
// ---------------------------------------------------------------------------
BOOL CCloudKeyExport::InitializeFromImportedKey(const DittoKeyData& keyData)
{
	// After ImportKey succeeds, the key is already stored in registry
	// Just read it back and initialize
	try
	{
		CString csKeyB64 = CGetSetOptions::GetCloudEncryptionKey();
		CT2A keyB64A(csKeyB64, CP_UTF8);
		CStringA keyBase64(keyB64A);
		if (keyBase64.IsEmpty())
		{
			return FALSE;
		}
		std::vector<BYTE> keyBytes = CCloudCrypto::Base64Decode(keyBase64);
		if (keyBytes.size() != 32)
		{
			return FALSE;
		}
		return CCloudCrypto::Initialize(keyBytes);
	}
	catch (...)
	{
		return FALSE;
	}
}

// ---------------------------------------------------------------------------
// IsValidKeyFile: verify .dittokey file format
// ---------------------------------------------------------------------------
BOOL CCloudKeyExport::IsValidKeyFile(const CString& filePath)
{
	try
	{
		CFile file;
		if (!file.Open(filePath, CFile::modeRead | CFile::typeBinary))
			return FALSE;

		ULONGLONG fileSize = file.GetLength();
		if (fileSize > 64 * 1024)
		{
			file.Close();
			return FALSE;
		}

		CStringA fileContent;
		LPSTR buf = fileContent.GetBuffer(static_cast<int>(fileSize) + 1);
		UINT bytesRead = file.Read(buf, static_cast<UINT>(fileSize));
		fileContent.ReleaseBuffer(bytesRead);
		file.Close();

		json keyJson = json::parse(fileContent.GetString());
		return keyJson.contains("version") &&
			   keyJson["version"].get<int>() == 1 &&
			   keyJson.contains("salt") &&
			   keyJson.contains("encrypted_key") &&
			   keyJson.contains("checksum");
	}
	catch (...)
	{
		return FALSE;
	}
}

// ---------------------------------------------------------------------------
// GetKeyFileInfo: read metadata without decrypting
// ---------------------------------------------------------------------------
BOOL CCloudKeyExport::GetKeyFileInfo(const CString& filePath, DittoKeyData& outInfo)
{
	try
	{
		CFile file;
		if (!file.Open(filePath, CFile::modeRead | CFile::typeBinary))
			return FALSE;

		ULONGLONG fileSize = file.GetLength();
		if (fileSize > 64 * 1024)
		{
			file.Close();
			return FALSE;
		}

		CStringA fileContent;
		LPSTR buf = fileContent.GetBuffer(static_cast<int>(fileSize) + 1);
		UINT bytesRead = file.Read(buf, static_cast<UINT>(fileSize));
		fileContent.ReleaseBuffer(bytesRead);
		file.Close();

		json keyJson = json::parse(fileContent.GetString());

		outInfo.version = keyJson.value("version", 0);
		outInfo.username = StdStringToCString(keyJson.value("username", ""));
		outInfo.createdAt = StdStringToCString(keyJson.value("created_at", ""));
		outInfo.salt = StdStringToCString(keyJson.value("salt", ""));
		outInfo.encryptedKey = StdStringToCString(keyJson.value("encrypted_key", ""));
		outInfo.checksum = StdStringToCString(keyJson.value("checksum", ""));

		return outInfo.version == 1;
	}
	catch (...)
	{
		return FALSE;
	}
}
