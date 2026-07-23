// CloudKeyExportTest.cpp - Unit tests for CloudKeyExport (.dittokey) module
// Tests: Export, Import, Validate, Roundtrip
//
// NOTE: These tests call the ACTUAL CCloudKeyExport::ExportKey() and 
// CCloudKeyExport::ImportKey() methods to ensure real code is tested.

#include "stdafx.h"
#include <gtest/gtest.h>
#include "../src/CloudSync/CloudCrypto.h"
#include "../src/CloudSync/CloudKeyExport.h"
#include "GetSetOptionsMock.h"
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
// Test Fixture: Setup/Teardown for CloudKeyExport tests
// ============================================================================

class CloudKeyExportTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		// Reset mock options before each test
		CGetSetOptions::Reset();
		CCloudCrypto::Reset();

		// Generate and store a test encryption key
		std::vector<BYTE> key = CCloudCrypto::RandomBytes(32);
		std::vector<BYTE> salt = CCloudCrypto::RandomBytes(32);
		CStringA keyB64 = CCloudCrypto::Base64Encode(key);
		CStringA saltB64 = CCloudCrypto::Base64Encode(salt);

		CGetSetOptions::SetCloudEncryptionKey(CString(keyB64));
		CGetSetOptions::SetCloudEncryptionSalt(CString(saltB64));
		CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);

		// Initialize crypto
		CCloudCrypto::Initialize(key);
	}

	void TearDown() override
	{
		// Clean up
		CCloudCrypto::Reset();
		CGetSetOptions::Reset();
	}

	// Helper to get stored key
	std::vector<BYTE> GetStoredKey()
	{
		CString keyB64 = CGetSetOptions::GetCloudEncryptionKey();
		CT2A keyA(keyB64, CP_UTF8);
		return CCloudCrypto::Base64Decode(CStringA(keyA));
	}
};

// ============================================================================
// IsValidKeyFile tests
// ============================================================================

TEST_F(CloudKeyExportTest, IsValidKeyFile_ValidFile)
{
	// Create a valid key file using ExportKey
	CString filePath = GetTempFilePath("valid_");
	CString username = _T("testuser");
	CString password = _T("TestPass123!");

	// Export creates the file
	BOOL exportOk = CCloudKeyExport::ExportKey(filePath, username, password);
	EXPECT_TRUE(exportOk) << "ExportKey should succeed";

	// Validate
	EXPECT_TRUE(CCloudKeyExport::IsValidKeyFile(filePath));

	DeleteFile(filePath);
}

TEST_F(CloudKeyExportTest, IsValidKeyFile_InvalidJson)
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

TEST_F(CloudKeyExportTest, IsValidKeyFile_NonExistentFile)
{
	EXPECT_FALSE(CCloudKeyExport::IsValidKeyFile(_T("C:\\NonExistent\\file.dittokey")));
}

TEST_F(CloudKeyExportTest, IsValidKeyFile_FileTooLarge)
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

TEST_F(CloudKeyExportTest, IsValidKeyFile_UnsupportedVersion)
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

TEST_F(CloudKeyExportTest, GetKeyFileInfo_ReadMetadata)
{
	// Export a key file first
	CString filePath = GetTempFilePath("meta_");
	CString username = _T("metadata_test_user");
	CString password = _T("Pass123!");

	ASSERT_TRUE(CCloudKeyExport::ExportKey(filePath, username, password));

	// Read metadata
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
// Full Export/Import Roundtrip - Using ACTUAL ExportKey/ImportKey methods
// ============================================================================

TEST_F(CloudKeyExportTest, Roundtrip_CorrectPassword)
{
	CString filePath = GetTempFilePath("roundtrip_");
	CString username = _T("roundtrip_test@example.com");
	CString password = _T("SecurePassword123!");

	// Get original key before export
	std::vector<BYTE> originalKey = GetStoredKey();

	// Export using ACTUAL CCloudKeyExport::ExportKey
	BOOL exportOk = CCloudKeyExport::ExportKey(filePath, username, password);
	EXPECT_TRUE(exportOk) << "ExportKey should succeed";

	// Validate
	EXPECT_TRUE(CCloudKeyExport::IsValidKeyFile(filePath));

	// Import using ACTUAL CCloudKeyExport::ImportKey
	DittoKeyData keyData;
	BOOL importOk = CCloudKeyExport::ImportKey(filePath, password, keyData);
	EXPECT_TRUE(importOk) << "ImportKey should succeed with correct password";

	if (importOk)
	{
		// Verify metadata
		EXPECT_EQ(1, keyData.version);
		EXPECT_STREQ(username, keyData.username);

		// Verify key was restored correctly
		std::vector<BYTE> restoredKey = GetStoredKey();
		EXPECT_EQ(originalKey.size(), restoredKey.size());
		EXPECT_EQ(0, memcmp(originalKey.data(), restoredKey.data(), originalKey.size()));

		// Verify crypto works with restored key
		CStringA testPlain = "Roundtrip test data - Hello World!";
		CStringA encrypted = CCloudCrypto::Encrypt(testPlain);
		EXPECT_FALSE(encrypted.IsEmpty());

		CStringA decrypted = CCloudCrypto::Decrypt(encrypted);
		EXPECT_STREQ(testPlain.GetString(), decrypted.GetString());
	}

	DeleteFile(filePath);
}

TEST_F(CloudKeyExportTest, Roundtrip_WrongPassword)
{
	CString filePath = GetTempFilePath("wrongpass_");
	CString username = _T("user");
	CString correctPassword = _T("CorrectPassword");
	CString wrongPassword = _T("WrongPassword");

	// Export with correct password
	ASSERT_TRUE(CCloudKeyExport::ExportKey(filePath, username, correctPassword));

	// Import with wrong password should fail
	DittoKeyData keyData;
	BOOL importOk = CCloudKeyExport::ImportKey(filePath, wrongPassword, keyData);
	EXPECT_FALSE(importOk) << "ImportKey should fail with wrong password";

	DeleteFile(filePath);
}

TEST_F(CloudKeyExportTest, Roundtrip_CorruptedFile)
{
	CString filePath = GetTempFilePath("corrupt_");
	CString username = _T("user");
	CString password = _T("Pass");

	// Export valid file first
	ASSERT_TRUE(CCloudKeyExport::ExportKey(filePath, username, password));

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
	DittoKeyData keyData;
	BOOL importOk = CCloudKeyExport::ImportKey(filePath, password, keyData);
	EXPECT_FALSE(importOk) << "ImportKey should fail with corrupted data";

	DeleteFile(filePath);
}

TEST_F(CloudKeyExportTest, Roundtrip_UnicodeContent)
{
	CString filePath = GetTempFilePath("unicode_");
	CString username = _T("用户测试@example.com");  // Chinese characters
	CString password = _T("密码测试123!");

	// Get original key
	std::vector<BYTE> originalKey = GetStoredKey();

	// Export
	ASSERT_TRUE(CCloudKeyExport::ExportKey(filePath, username, password));

	// Import
	DittoKeyData keyData;
	BOOL importOk = CCloudKeyExport::ImportKey(filePath, password, keyData);
	EXPECT_TRUE(importOk);

	if (importOk)
	{
		EXPECT_STREQ(username, keyData.username);

		// Verify key matches original
		std::vector<BYTE> restoredKey = GetStoredKey();
		EXPECT_EQ(originalKey.size(), restoredKey.size());
		EXPECT_EQ(0, memcmp(originalKey.data(), restoredKey.data(), originalKey.size()));
	}

	DeleteFile(filePath);
}

// ============================================================================
// Multiple Export/Import Cycles (stress test)
// ============================================================================

TEST_F(CloudKeyExportTest, Roundtrip_MultipleCycles)
{
	CString password = _T("MultiCycleTest!");
	CString username = _T("multi_cycle_user");

	for (int i = 0; i < 5; i++)
	{
		CString filePath = GetTempFilePath("cycle_");
		
		// Get current key
		std::vector<BYTE> originalKey = GetStoredKey();

		// Export
		ASSERT_TRUE(CCloudKeyExport::ExportKey(filePath, username, password));

		// Import
		DittoKeyData keyData;
		BOOL importOk = CCloudKeyExport::ImportKey(filePath, password, keyData);
		EXPECT_TRUE(importOk) << "Cycle " << i << " import failed";

		if (importOk)
		{
			// Verify key matches
			std::vector<BYTE> restoredKey = GetStoredKey();
			EXPECT_EQ(0, memcmp(originalKey.data(), restoredKey.data(), originalKey.size()));

			// Verify crypto works
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

TEST_F(CloudKeyExportTest, Roundtrip_EmptyPassword)
{
	CString filePath = GetTempFilePath("emptypass_");
	CString username = _T("empty_pass_user");
	CString password = _T("");

	// Get original key
	std::vector<BYTE> originalKey = GetStoredKey();

	// Export with empty password (should still work, just less secure)
	ASSERT_TRUE(CCloudKeyExport::ExportKey(filePath, username, password));

	// Import with same empty password
	DittoKeyData keyData;
	BOOL importOk = CCloudKeyExport::ImportKey(filePath, password, keyData);
	EXPECT_TRUE(importOk) << "ImportKey should work with empty password";

	if (importOk)
	{
		// Verify key matches
		std::vector<BYTE> restoredKey = GetStoredKey();
		EXPECT_EQ(originalKey.size(), restoredKey.size());
		EXPECT_EQ(0, memcmp(originalKey.data(), restoredKey.data(), originalKey.size()));
	}

	DeleteFile(filePath);
}

// ============================================================================
// InitializeFromImportedKey tests
// ============================================================================

TEST_F(CloudKeyExportTest, InitializeFromImportedKey_AfterImport)
{
	CString filePath = GetTempFilePath("init_");
	CString username = _T("init_test_user");
	CString password = _T("InitTest123!");

	// Export
	ASSERT_TRUE(CCloudKeyExport::ExportKey(filePath, username, password));

	// Import (this should store the key and initialize crypto)
	DittoKeyData keyData;
	BOOL importOk = CCloudKeyExport::ImportKey(filePath, password, keyData);
	ASSERT_TRUE(importOk);

	// Now call InitializeFromImportedKey (should work since key is stored)
	BOOL initOk = CCloudKeyExport::InitializeFromImportedKey(keyData);
	EXPECT_TRUE(initOk) << "InitializeFromImportedKey should succeed after import";

	DeleteFile(filePath);
}

TEST_F(CloudKeyExportTest, InitializeFromImportedKey_WithoutImport)
{
	// Reset state
	CGetSetOptions::Reset();
	CCloudCrypto::Reset();

	// Try to initialize without importing
	DittoKeyData keyData;
	keyData.version = 1;
	keyData.username = _T("test");
	keyData.salt = _T("abc");
	keyData.encryptedKey = _T("xyz");
	keyData.checksum = _T("chk");

	BOOL initOk = CCloudKeyExport::InitializeFromImportedKey(keyData);
	EXPECT_FALSE(initOk) << "InitializeFromImportedKey should fail without prior import";
}
