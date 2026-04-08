// CloudKeyExportTest.cpp - Unit tests for CloudKeyExport (.dittokey) module
// Tests: Export, Import, Validate, Roundtrip
//
// Note: CGetSetOptions requires a running MFC app instance, so we test the
// crypto core of Export/Import directly rather than through the full API.

#include "stdafx.h"
#include <gtest/gtest.h>
#include "../src/CloudSync/CloudCrypto.h"
#include "../src/httplib.h"
#include "../src/json.hpp"
#include <vector>
#include <string>
#include <cstring>

using json = nlohmann::json;

// ============================================================================
// Helper: generate a temporary file path
// ============================================================================
static CString GetTempFilePath(const char* prefix = "dittokey_test_")
{
	char tempPath[MAX_PATH];
	DWORD len = GetTempPathA(MAX_PATH, tempPath);
	EXPECT_GT(len, 0u);

	static int counter = 0;
	counter++;
	char fileName[MAX_PATH];
	sprintf_s(fileName, "%s%s%d.dittokey", tempPath, prefix, counter);
	return CString(fileName);
}

// ============================================================================
// Helper: test key storage (bypasses CGetSetOptions registry calls)
// ============================================================================
static std::vector<BYTE> g_testKey;
static std::vector<BYTE> g_testSalt;
static CStringA g_testKeyB64;
static CStringA g_testSaltB64;

static void SetupTestKey()
{
	g_testKey = CCloudCrypto::RandomBytes(32);
	g_testSalt = CCloudCrypto::RandomBytes(32);
	g_testKeyB64 = CCloudCrypto::Base64Encode(g_testKey);
	g_testSaltB64 = CCloudCrypto::Base64Encode(g_testSalt);
	CCloudCrypto::Initialize(g_testKey);
}

// ============================================================================
// Helper: Create a .dittokey file using the same logic as CloudKeyExport::ExportKey
// but without CGetSetOptions dependency
// ============================================================================
static BOOL CreateTestKeyFile(const CString& filePath,
                              const CString& username,
                              const CString& password)
{
	// Compute checksum (SHA-256 of original key)
	std::vector<BYTE> checksum = CCloudCrypto::Sha256(g_testKey);
	CStringA checksumB64 = CCloudCrypto::Base64Encode(checksum);

	// Derive a key from password + salt for encrypting the exported key
	std::vector<BYTE> exportKey = CCloudCrypto::DeriveKey(
		CStringA(password), g_testSalt, 100000);

	// Encrypt the AES key with the password-derived key
	std::vector<BYTE> iv = CCloudCrypto::RandomBytes(12);
	std::vector<BYTE> tag;
	std::vector<BYTE> encryptedKey = CCloudCrypto::AesGcmEncrypt(
		exportKey, iv, g_testKey, tag);

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
	keyJson["username"] = CT2A(username, CP_UTF8).m_psz;
	keyJson["created_at"] = CT2A(timestamp, CP_UTF8).m_psz;
	keyJson["salt"] = g_testSaltB64.GetString();
	keyJson["encrypted_key"] = encryptedPayloadB64.GetString();
	keyJson["checksum"] = checksumB64.GetString();

	std::string keyStr = keyJson.dump(2);

	// Write to file
	CStringA keyStrA(keyStr.c_str());
	CFile file;
	if (!file.Open(filePath, CFile::modeCreate | CFile::modeWrite | CFile::typeBinary))
		return FALSE;

	file.Write(keyStrA.GetString(), keyStrA.GetLength());
	file.Close();
	return TRUE;
}

// ============================================================================
// Helper: Import and decrypt a .dittokey file (bypasses CGetSetOptions)
// Returns the decrypted key if successful
// ============================================================================
static BOOL ImportTestKey(const CString& filePath,
                          const CString& password,
                          std::vector<BYTE>& outKey,
                          DittoKeyData& outKeyData)
{
	try
	{
		// Read file
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

		// Parse JSON
		json keyJson = json::parse(fileContent.GetString());

		if (!keyJson.contains("version") || keyJson["version"].get<int>() != 1)
			return FALSE;

		outKeyData.version = keyJson["version"].get<int>();

		auto strField = [&](const char* name) -> CString {
			if (keyJson.contains(name) && !keyJson[name].is_null())
				return CString(keyJson[name].get<std::string>().c_str());
			return _T("");
		};

		outKeyData.username = strField("username");
		outKeyData.createdAt = strField("created_at");
		outKeyData.salt = strField("salt");
		outKeyData.encryptedKey = strField("encrypted_key");
		outKeyData.checksum = strField("checksum");

		// Decrypt the key
		std::vector<BYTE> encryptedPayload = CCloudCrypto::Base64Decode(CStringA(outKeyData.encryptedKey));
		if (encryptedPayload.size() < 12 + 16)
			return FALSE;

		std::vector<BYTE> iv(encryptedPayload.begin(), encryptedPayload.begin() + 12);
		std::vector<BYTE> tag(encryptedPayload.end() - 16, encryptedPayload.end());
		std::vector<BYTE> ciphertext(encryptedPayload.begin() + 12, encryptedPayload.end() - 16);

		std::vector<BYTE> saltBytes = CCloudCrypto::Base64Decode(CStringA(outKeyData.salt));
		std::vector<BYTE> exportKey = CCloudCrypto::DeriveKey(
			CStringA(password), saltBytes, 100000);

		outKey = CCloudCrypto::AesGcmDecrypt(exportKey, iv, ciphertext, tag);
		if (outKey.empty())
			return FALSE;

		// Verify checksum
		std::vector<BYTE> expectedChecksum = CCloudCrypto::Base64Decode(CStringA(outKeyData.checksum));
		std::vector<BYTE> actualChecksum = CCloudCrypto::Sha256(outKey);
		if (expectedChecksum != actualChecksum)
			return FALSE;

		return TRUE;
	}
	catch (...)
	{
		return FALSE;
	}
}

// ============================================================================
// IsValidKeyFile tests
// ============================================================================

TEST(CloudKeyExport_IsValidKeyFile, ValidFile)
{
	SetupTestKey();
	CString filePath = GetTempFilePath("valid_");
	ASSERT_TRUE(CreateTestKeyFile(filePath, _T("testuser"), _T("TestPass123!")));

	EXPECT_TRUE(CCloudKeyExport::IsValidKeyFile(filePath));

	DeleteFile(filePath);
}

TEST(CloudKeyExport_IsValidKeyFile, InvalidJson)
{
	CString filePath = GetTempFilePath("invalid_");
	CStringA invalidContent = "This is not a valid key file";
	CFile file;
	ASSERT_TRUE(file.Open(filePath, CFile::modeCreate | CFile::modeWrite | CFile::typeBinary));
	file.Write(invalidContent.GetString(), invalidContent.GetLength());
	file.Close();

	EXPECT_FALSE(CCloudKeyExport::IsValidKeyFile(filePath));
	DeleteFile(filePath);
}

TEST(CloudKeyExport_IsValidKeyFile, NonExistentFile)
{
	EXPECT_FALSE(CCloudKeyExport::IsValidKeyFile(_T("C:\\NonExistent\\file.dittokey")));
}

TEST(CloudKeyExport_IsValidKeyFile, FileTooLarge)
{
	CString filePath = GetTempFilePath("large_");
	CFile file;
	ASSERT_TRUE(file.Open(filePath, CFile::modeCreate | CFile::modeWrite | CFile::typeBinary));
	const int size = 65 * 1024; // 65KB, exceeds 64KB limit
	std::vector<char> garbage(size, 'X');
	file.Write(garbage.data(), size);
	file.Close();

	EXPECT_FALSE(CCloudKeyExport::IsValidKeyFile(filePath));
	DeleteFile(filePath);
}

TEST(CloudKeyExport_IsValidKeyFile, UnsupportedVersion)
{
	CString filePath = GetTempFilePath("ver_");
	json keyJson;
	keyJson["version"] = 99;
	keyJson["username"] = "future_user";
	keyJson["created_at"] = "2099-01-01T00:00:00Z";
	keyJson["salt"] = "abc";
	keyJson["encrypted_key"] = "xyz";
	keyJson["checksum"] = "chk";

	CStringA keyStrA(keyJson.dump(2).c_str());
	CFile file;
	ASSERT_TRUE(file.Open(filePath, CFile::modeCreate | CFile::modeWrite | CFile::typeBinary));
	file.Write(keyStrA.GetString(), keyStrA.GetLength());
	file.Close();

	// Should not be valid (version != 1)
	EXPECT_FALSE(CCloudKeyExport::IsValidKeyFile(filePath));
	DeleteFile(filePath);
}

// ============================================================================
// GetKeyFileInfo tests
// ============================================================================

TEST(CloudKeyExport_GetKeyFileInfo, ReadMetadata)
{
	SetupTestKey();
	CString filePath = GetTempFilePath("meta_");
	CString username = _T("metadata_test_user");
	ASSERT_TRUE(CreateTestKeyFile(filePath, username, _T("Pass123!")));

	DittoKeyData info;
	BOOL ok = CCloudKeyExport::GetKeyFileInfo(filePath, info);
	EXPECT_TRUE(ok);
	EXPECT_EQ(1, info.version);
	EXPECT_STREQ(username, info.username);
	EXPECT_FALSE(info.createdAt.IsEmpty());
	EXPECT_FALSE(info.salt.IsEmpty());

	DeleteFile(filePath);
}

// ============================================================================
// Full Export/Import Roundtrip
// ============================================================================

TEST(CloudKeyExport_Roundtrip, CorrectPassword)
{
	SetupTestKey();
	CString filePath = GetTempFilePath("roundtrip_");
	CString username = _T("roundtrip_test@example.com");
	CString password = _T("SecurePassword123!");

	// Create export file
	ASSERT_TRUE(CreateTestKeyFile(filePath, username, password));

	// Validate
	EXPECT_TRUE(CCloudKeyExport::IsValidKeyFile(filePath));

	// Import
	std::vector<BYTE> importedKey;
	DittoKeyData keyData;
	BOOL ok = ImportTestKey(filePath, password, importedKey, keyData);
	EXPECT_TRUE(ok) << "Import failed with correct password";

	if (ok)
	{
		// Verify metadata
		EXPECT_EQ(1, keyData.version);
		EXPECT_STREQ(username, keyData.username);

		// Verify key matches original
		EXPECT_EQ(g_testKey.size(), importedKey.size());
		EXPECT_EQ(0, memcmp(g_testKey.data(), importedKey.data(), g_testKey.size()));

		// Verify crypto works with imported key
		CCloudCrypto::Initialize(importedKey);
		CStringA testPlain = "Roundtrip test data - Hello World!";
		CStringA encrypted = CCloudCrypto::Encrypt(testPlain);
		EXPECT_FALSE(encrypted.IsEmpty());

		CStringA decrypted = CCloudCrypto::Decrypt(encrypted);
		EXPECT_STREQ(testPlain.GetString(), decrypted.GetString());
	}

	DeleteFile(filePath);
}

TEST(CloudKeyExport_Roundtrip, WrongPassword)
{
	SetupTestKey();
	CString filePath = GetTempFilePath("wrongpass_");
	ASSERT_TRUE(CreateTestKeyFile(filePath, _T("user"), _T("CorrectPassword")));

	// Import with wrong password
	std::vector<BYTE> importedKey;
	DittoKeyData keyData;
	BOOL ok = ImportTestKey(filePath, _T("WrongPassword"), importedKey, keyData);
	EXPECT_FALSE(ok) << "Import should have failed with wrong password";

	DeleteFile(filePath);
}

TEST(CloudKeyExport_Roundtrip, CorruptedFile)
{
	SetupTestKey();
	CString filePath = GetTempFilePath("corrupt_");

	// Create valid file first
	ASSERT_TRUE(CreateTestKeyFile(filePath, _T("user"), _T("Pass")));

	// Now corrupt the encrypted_key field
	CFile file;
	ASSERT_TRUE(file.Open(filePath, CFile::modeRead | CFile::typeBinary));
	ULONGLONG fileSize = file.GetLength();
	CStringA fileContent;
	LPSTR buf = fileContent.GetBuffer(static_cast<int>(fileSize) + 1);
	UINT bytesRead = file.Read(buf, static_cast<UINT>(fileSize));
	fileContent.ReleaseBuffer(bytesRead);
	file.Close();

	json keyJson = json::parse(fileContent.GetString());
	keyJson["encrypted_key"] = "AAAAAAAAAAAAAAAAAAAAAA=="; // Corrupt
	keyJson["checksum"] = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";

	CStringA corruptedStr(keyJson.dump(2).c_str());
	ASSERT_TRUE(file.Open(filePath, CFile::modeCreate | CFile::modeWrite | CFile::typeBinary));
	file.Write(corruptedStr.GetString(), corruptedStr.GetLength());
	file.Close();

	// Import should fail
	std::vector<BYTE> importedKey;
	DittoKeyData keyData;
	BOOL ok = ImportTestKey(filePath, _T("Pass"), importedKey, keyData);
	EXPECT_FALSE(ok) << "Import should fail with corrupted data";

	DeleteFile(filePath);
}

TEST(CloudKeyExport_Roundtrip, UnicodeContent)
{
	SetupTestKey();
	CString filePath = GetTempFilePath("unicode_");
	CString username = _T("用户测试@example.com");  // Chinese characters
	CString password = _T("密码测试123!");

	ASSERT_TRUE(CreateTestKeyFile(filePath, username, password));

	std::vector<BYTE> importedKey;
	DittoKeyData keyData;
	BOOL ok = ImportTestKey(filePath, password, importedKey, keyData);
	EXPECT_TRUE(ok);

	if (ok)
	{
		EXPECT_STREQ(username, keyData.username);
		EXPECT_EQ(g_testKey.size(), importedKey.size());
		EXPECT_EQ(0, memcmp(g_testKey.data(), importedKey.data(), g_testKey.size()));
	}

	DeleteFile(filePath);
}

// ============================================================================
// Multiple Export/Import Cycles (stress test)
// ============================================================================

TEST(CloudKeyExport_Roundtrip, MultipleCycles)
{
	SetupTestKey();
	CString password = _T("MultiCycleTest!");
	CString username = _T("multi_cycle_user");

	for (int i = 0; i < 5; i++)
	{
		CString filePath = GetTempFilePath("cycle_");
		ASSERT_TRUE(CreateTestKeyFile(filePath, username, password));

		std::vector<BYTE> importedKey;
		DittoKeyData keyData;
		BOOL ok = ImportTestKey(filePath, password, importedKey, keyData);
		EXPECT_TRUE(ok) << "Cycle " << i << " import failed";

		if (ok)
		{
			// Verify key matches
			EXPECT_EQ(0, memcmp(g_testKey.data(), importedKey.data(), g_testKey.size()));

			// Verify crypto works
			CCloudCrypto::Initialize(importedKey);
			CStringA testPlain = "Cycle test data";
			CStringA encrypted = CCloudCrypto::Encrypt(testPlain);
			CStringA decrypted = CCloudCrypto::Decrypt(encrypted);
			EXPECT_STREQ(testPlain.GetString(), decrypted.GetString());
		}

		DeleteFile(filePath);
	}
}

// ============================================================================
// Empty password test
// ============================================================================

TEST(CloudKeyExport_Roundtrip, EmptyPassword)
{
	SetupTestKey();
	CString filePath = GetTempFilePath("emptypass_");

	// Export with empty password (should still work, just less secure)
	CString username = _T("empty_pass_user");
	CString password = _T("");

	ASSERT_TRUE(CreateTestKeyFile(filePath, username, password));

	// Import with same empty password
	std::vector<BYTE> importedKey;
	DittoKeyData keyData;
	BOOL ok = ImportTestKey(filePath, password, importedKey, keyData);
	EXPECT_TRUE(ok) << "Import should work with empty password";

	DeleteFile(filePath);
}
