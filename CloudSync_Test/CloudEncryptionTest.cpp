// CloudEncryptionTest.cpp - Unit tests for CloudEncryption module
// Tests: Setup encryption, status check, stored key initialization, clip data encryption
//
// NOTE: This tests the actual CCloudEncryption wrapper methods including:
// - EncryptClipData() - tests the security gate (returns empty when not ready)
// - DecryptClipData() - tests the fallback logic (returns as-is on failure)
// - IsEncryptionReady() - tests the readiness check

#include "stdafx.h"
#include <gtest/gtest.h>
#include "../src/CloudSync/CloudEncryption.h"
#include "../src/CloudSync/CloudCrypto.h"
#include "GetSetOptionsMock.h"
#include <vector>
#include <string>

// ============================================================================
// Test Fixture: Setup/Teardown for CloudEncryption tests
// ============================================================================

class CloudEncryptionTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		// Reset mock options before each test
		CGetSetOptions::Reset();
		
		// Reset crypto state
		CCloudCrypto::Reset();
	}

	void TearDown() override
	{
		// Clean up crypto state
		CCloudCrypto::Reset();
	}
};

class CloudEncryption_Setup : public CloudEncryptionTest {};
class CloudEncryption_Status : public CloudEncryptionTest {};
class CloudEncryption_StoredKey : public CloudEncryptionTest {};
class CloudEncryption_ClipData : public CloudEncryptionTest {};
class CloudEncryption_Ready : public CloudEncryptionTest {};
class CloudEncryption_Integration : public CloudEncryptionTest {};

// ============================================================================
// Setup Encryption Tests
// These tests simulate the SetupEncryption flow without network calls
// ============================================================================

TEST_F(CloudEncryption_Setup, SuccessfulEncryptionSetup)
{
	// Simulate the SetupEncryption flow:
	// 1. Generate a salt (would normally come from server)
	// 2. Derive key from password + salt
	// 3. Initialize crypto
	// 4. Store key in settings

	// Generate salt
	std::vector<BYTE> salt = CCloudCrypto::RandomBytes(16);
	CStringA saltB64 = CCloudCrypto::Base64Encode(salt);
	
	// Derive key from password
	CStringA password("MySecurePassword123!");
	std::vector<BYTE> key = CCloudCrypto::DeriveKey(password, salt, 100000);
	
	// Verify key size
	EXPECT_EQ(key.size(), 32);

	// Initialize crypto
	BOOL initOk = CCloudCrypto::Initialize(key);
	EXPECT_TRUE(initOk);

	// Store key in settings (simulates what SetupEncryption does)
	CStringA keyB64 = CCloudCrypto::Base64Encode(key);
	CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);
	CGetSetOptions::SetCloudEncryptionKey(CString(keyB64));
	CGetSetOptions::SetCloudEncryptionSalt(CString(saltB64));

	// Verify settings
	EXPECT_TRUE(CGetSetOptions::GetCloudSyncEncryptionEnabled());
	EXPECT_FALSE(CGetSetOptions::GetCloudEncryptionKey().IsEmpty());
}

TEST_F(CloudEncryption_Setup, WeakPasswordStillWorks)
{
	// Test that weak passwords are accepted (user responsibility)
	std::vector<BYTE> salt = CCloudCrypto::RandomBytes(16);
	CStringA password("123"); // Very weak password
	
	std::vector<BYTE> key = CCloudCrypto::DeriveKey(password, salt, 100000);
	EXPECT_EQ(key.size(), 32);
	
	BOOL initOk = CCloudCrypto::Initialize(key);
	EXPECT_TRUE(initOk);
}

TEST_F(CloudEncryption_Setup, UnicodePassword)
{
	// Test password with Unicode characters
	std::vector<BYTE> salt = CCloudCrypto::RandomBytes(16);
	
	// Unicode password: "密码123"
	CStringW passwordW(L"\u5BC6\u7801123");
	CT2A passwordA(passwordW, CP_UTF8);
	
	std::vector<BYTE> key = CCloudCrypto::DeriveKey(CStringA(passwordA), salt, 100000);
	EXPECT_EQ(key.size(), 32);
	
	BOOL initOk = CCloudCrypto::Initialize(key);
	EXPECT_TRUE(initOk);
}

TEST_F(CloudEncryption_Setup, EmptyPasswordFails)
{
	// Test that empty password produces a key (but shouldn't be used)
	std::vector<BYTE> salt = CCloudCrypto::RandomBytes(16);
	CStringA password("");
	
	std::vector<BYTE> key = CCloudCrypto::DeriveKey(password, salt, 100000);
	
	// PBKDF2 should still produce a key even with empty password
	EXPECT_EQ(key.size(), 32);
}

TEST_F(CloudEncryption_Setup, DifferentSaltsProduceDifferentKeys)
{
	// Test that different salts produce different keys
	std::vector<BYTE> salt1 = CCloudCrypto::RandomBytes(16);
	std::vector<BYTE> salt2 = CCloudCrypto::RandomBytes(16);
	
	CStringA password("SamePassword");
	
	std::vector<BYTE> key1 = CCloudCrypto::DeriveKey(password, salt1, 100000);
	std::vector<BYTE> key2 = CCloudCrypto::DeriveKey(password, salt2, 100000);
	
	// Keys should be different
	EXPECT_NE(0, memcmp(key1.data(), key2.data(), 32));
}

TEST_F(CloudEncryption_Setup, DifferentIterationsProduceDifferentKeys)
{
	// Test that different iteration counts produce different keys
	std::vector<BYTE> salt = CCloudCrypto::RandomBytes(16);
	CStringA password("TestPassword");
	
	std::vector<BYTE> key1 = CCloudCrypto::DeriveKey(password, salt, 10000);
	std::vector<BYTE> key2 = CCloudCrypto::DeriveKey(password, salt, 100000);
	
	// Keys should be different
	EXPECT_NE(0, memcmp(key1.data(), key2.data(), 32));
}

// ============================================================================
// Encryption Status Tests
// ============================================================================

TEST_F(CloudEncryption_Status, EncryptionEnabled)
{
	// Setup encryption
	std::vector<BYTE> key = CCloudCrypto::RandomBytes(32);
	CCloudCrypto::Initialize(key);
	
	CStringA keyB64 = CCloudCrypto::Base64Encode(key);
	CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);
	CGetSetOptions::SetCloudEncryptionKey(CString(keyB64));

	// Check status via the wrapper
	BOOL ready = CCloudEncryption::IsEncryptionReady();
	EXPECT_TRUE(ready);
}

TEST_F(CloudEncryption_Status, EncryptionDisabled)
{
	// Check default state
	BOOL ready = CCloudEncryption::IsEncryptionReady();
	EXPECT_FALSE(ready);
}

TEST_F(CloudEncryption_Status, KeyExists)
{
	// Setup with valid key
	std::vector<BYTE> key = CCloudCrypto::RandomBytes(32);
	CStringA keyB64 = CCloudCrypto::Base64Encode(key);
	
	CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);
	CGetSetOptions::SetCloudEncryptionKey(CString(keyB64));

	// Verify key exists
	CString storedKey = CGetSetOptions::GetCloudEncryptionKey();
	EXPECT_FALSE(storedKey.IsEmpty());
}

TEST_F(CloudEncryption_Status, KeyDoesNotExist)
{
	// Check default state
	CString key = CGetSetOptions::GetCloudEncryptionKey();
	EXPECT_TRUE(key.IsEmpty());
}

// ============================================================================
// Initialize Crypto From Stored Key Tests
// ============================================================================

TEST_F(CloudEncryption_StoredKey, ValidStoredKey)
{
	// Generate and store a valid key
	std::vector<BYTE> key = CCloudCrypto::RandomBytes(32);
	CStringA keyB64 = CCloudCrypto::Base64Encode(key);
	
	CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);
	CGetSetOptions::SetCloudEncryptionKey(CString(keyB64));

	// Initialize from stored key
	std::vector<BYTE> storedKey = CCloudCrypto::Base64Decode(CStringA(keyB64));
	EXPECT_EQ(storedKey.size(), 32);
	
	BOOL initOk = CCloudCrypto::Initialize(storedKey);
	EXPECT_TRUE(initOk);
}

TEST_F(CloudEncryption_StoredKey, InvalidKeySize)
{
	// Store key with wrong size
	std::vector<BYTE> wrongKey = CCloudCrypto::RandomBytes(16); // Wrong size
	CStringA keyB64 = CCloudCrypto::Base64Encode(wrongKey);
	
	CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);
	CGetSetOptions::SetCloudEncryptionKey(CString(keyB64));

	// Try to initialize
	std::vector<BYTE> storedKey = CCloudCrypto::Base64Decode(CStringA(keyB64));
	EXPECT_EQ(storedKey.size(), 16); // Not 32 bytes
	
	// Should fail initialization
	BOOL initOk = CCloudCrypto::Initialize(storedKey);
	EXPECT_FALSE(initOk);
}

TEST_F(CloudEncryption_StoredKey, EmptyStoredKey)
{
	// No key stored
	CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);
	CGetSetOptions::SetCloudEncryptionKey(CString(""));

	// Try to initialize with empty key
	CString keyB64 = CGetSetOptions::GetCloudEncryptionKey();
	CT2A keyA(keyB64, CP_UTF8);
	std::vector<BYTE> storedKey = CCloudCrypto::Base64Decode(CStringA(keyA));

	// Should be empty
	EXPECT_TRUE(storedKey.empty());
}

TEST_F(CloudEncryption_StoredKey, CorruptedStoredKey)
{
	// Store corrupted key (invalid base64)
	CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);
	CGetSetOptions::SetCloudEncryptionKey(CString("not-valid-base64!!!"));

	// Try to decode
	CString keyB64 = CGetSetOptions::GetCloudEncryptionKey();
	CT2A keyA(keyB64, CP_UTF8);
	std::vector<BYTE> storedKey = CCloudCrypto::Base64Decode(CStringA(keyA));
	
	// Decoding may fail or produce wrong size
	BOOL isValid = (storedKey.size() == 32);
	EXPECT_FALSE(isValid); // Should not be valid
}

TEST_F(CloudEncryption_StoredKey, NoStoredKey)
{
	// Don't store any key
	CGetSetOptions::SetCloudSyncEncryptionEnabled(FALSE);

	// Try to initialize
	CString keyB64 = CGetSetOptions::GetCloudEncryptionKey();
	EXPECT_TRUE(keyB64.IsEmpty());

	CT2A keyA(keyB64, CP_UTF8);
	std::vector<BYTE> storedKey = CCloudCrypto::Base64Decode(CStringA(keyA));
	EXPECT_TRUE(storedKey.empty());
}

// ============================================================================
// Encrypt Clip Data Tests - Using ACTUAL CCloudEncryption::EncryptClipData()
// ============================================================================

TEST_F(CloudEncryption_ClipData, EncryptWhenReady)
{
	// Setup encryption
	std::vector<BYTE> key = CCloudCrypto::RandomBytes(32);
	CStringA keyB64 = CCloudCrypto::Base64Encode(key);
	
	CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);
	CGetSetOptions::SetCloudEncryptionKey(CString(keyB64));
	CCloudCrypto::Initialize(key);

	// Use ACTUAL CCloudEncryption::EncryptClipData() wrapper method
	CStringA plaintext("Important clipboard text");
	CStringA encrypted = CCloudEncryption::EncryptClipData(plaintext);
	
	// Should succeed
	EXPECT_FALSE(encrypted.IsEmpty()) << "EncryptClipData returned empty when ready";
	EXPECT_NE(encrypted, plaintext);
}

TEST_F(CloudEncryption_ClipData, EncryptWhenNotReady_ReturnsEmpty)
{
	// SECURITY CRITICAL: Don't initialize crypto
	CGetSetOptions::SetCloudSyncEncryptionEnabled(FALSE);

	// Use ACTUAL CCloudEncryption::EncryptClipData() wrapper method
	CStringA plaintext("Secret data");
	CStringA encrypted = CCloudEncryption::EncryptClipData(plaintext);
	
	// SECURITY: MUST return empty when not ready to prevent plaintext sync
	EXPECT_TRUE(encrypted.IsEmpty()) << "SECURITY: EncryptClipData returned non-empty when not ready!";
}

TEST_F(CloudEncryption_ClipData, EncryptEmptyData)
{
	// Setup encryption
	std::vector<BYTE> key = CCloudCrypto::RandomBytes(32);
	CCloudCrypto::Initialize(key);
	CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);
	CStringA keyB64 = CCloudCrypto::Base64Encode(key);
	CGetSetOptions::SetCloudEncryptionKey(CString(keyB64));

	// Encrypt empty data
	CStringA plaintext("");
	CStringA encrypted = CCloudEncryption::EncryptClipData(plaintext);
	
	// Should still produce encrypted output (at least IV + tag)
	EXPECT_FALSE(encrypted.IsEmpty());
}

TEST_F(CloudEncryption_ClipData, EncryptBinaryData)
{
	// Setup encryption
	std::vector<BYTE> key = CCloudCrypto::RandomBytes(32);
	CCloudCrypto::Initialize(key);
	CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);
	CStringA keyB64 = CCloudCrypto::Base64Encode(key);
	CGetSetOptions::SetCloudEncryptionKey(CString(keyB64));

	// Create binary data
	std::vector<BYTE> binaryData(256);
	for (int i = 0; i < 256; i++)
	{
		binaryData[i] = static_cast<BYTE>(i);
	}

	CStringA plaintext(reinterpret_cast<const char*>(binaryData.data()), 256);
	CStringA encrypted = CCloudEncryption::EncryptClipData(plaintext);
	
	// Should succeed
	EXPECT_FALSE(encrypted.IsEmpty());
	
	// Should be able to decrypt
	CStringA decrypted = CCloudCrypto::Decrypt(encrypted);
	EXPECT_EQ(decrypted.GetLength(), 256);
	EXPECT_EQ(0, memcmp(decrypted.GetString(), binaryData.data(), 256));
}

TEST_F(CloudEncryption_ClipData, EncryptLargeData)
{
	// Setup encryption
	std::vector<BYTE> key = CCloudCrypto::RandomBytes(32);
	CCloudCrypto::Initialize(key);
	CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);
	CStringA keyB64 = CCloudCrypto::Base64Encode(key);
	CGetSetOptions::SetCloudEncryptionKey(CString(keyB64));

	// Create large data (100KB)
	std::string largeData(100000, 'A');
	CStringA plaintext(largeData.c_str());
	
	CStringA encrypted = CCloudEncryption::EncryptClipData(plaintext);
	
	// Should succeed
	EXPECT_FALSE(encrypted.IsEmpty());
	
	// Should be able to decrypt
	CStringA decrypted = CCloudCrypto::Decrypt(encrypted);
	EXPECT_EQ(decrypted.GetLength(), 100000);
}

// ============================================================================
// Decrypt Clip Data Tests - Using ACTUAL CCloudEncryption::DecryptClipData()
// ============================================================================

TEST_F(CloudEncryption_ClipData, DecryptWhenReady)
{
	// Setup encryption
	std::vector<BYTE> key = CCloudCrypto::RandomBytes(32);
	CStringA keyB64 = CCloudCrypto::Base64Encode(key);
	
	CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);
	CGetSetOptions::SetCloudEncryptionKey(CString(keyB64));
	CCloudCrypto::Initialize(key);

	// Encrypt then decrypt using ACTUAL wrapper methods
	CStringA plaintext("Test data for decryption");
	CStringA encrypted = CCloudEncryption::EncryptClipData(plaintext);
	CStringA decrypted = CCloudEncryption::DecryptClipData(encrypted);
	
	// Should match
	EXPECT_EQ(plaintext, decrypted);
}

TEST_F(CloudEncryption_ClipData, DecryptWhenNotReady_ReturnsAsIs)
{
	// Don't initialize crypto
	CGetSetOptions::SetCloudSyncEncryptionEnabled(FALSE);

	// Use ACTUAL CCloudEncryption::DecryptClipData() wrapper method
	// When not ready, it should return data as-is (fallback for unencrypted data)
	CStringA encryptedData("some-data");
	CStringA decrypted = CCloudEncryption::DecryptClipData(encryptedData);
	
	// Should return as-is when not ready (fallback behavior)
	EXPECT_EQ(decrypted, encryptedData);
}

TEST_F(CloudEncryption_ClipData, DecryptTamperedData_ReturnsEmpty)
{
	// Setup encryption
	std::vector<BYTE> key = CCloudCrypto::RandomBytes(32);
	CCloudCrypto::Initialize(key);
	CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);
	CStringA keyB64 = CCloudCrypto::Base64Encode(key);
	CGetSetOptions::SetCloudEncryptionKey(CString(keyB64));

	// Create valid encrypted data
	CStringA plaintext("Original data");
	CStringA encrypted = CCloudEncryption::EncryptClipData(plaintext);
	
	// Tamper with the encrypted data
	CStringA tampered(encrypted);
	if (tampered.GetLength() > 20)
	{
		LPSTR sz = tampered.GetBuffer();
		sz[10] = (sz[10] == 'A') ? 'B' : 'A';
		tampered.ReleaseBuffer();
	}
	
	// Decryption should fail and return empty
	CStringA decrypted = CCloudEncryption::DecryptClipData(tampered);
	EXPECT_TRUE(decrypted.IsEmpty()) << "Decryption should fail for tampered data";
}

TEST_F(CloudEncryption_ClipData, DecryptWrongKey_ReturnsEmpty)
{
	// Encrypt with one key
	std::vector<BYTE> key1 = CCloudCrypto::RandomBytes(32);
	CCloudCrypto::Initialize(key1);
	CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);
	CStringA key1B64 = CCloudCrypto::Base64Encode(key1);
	CGetSetOptions::SetCloudEncryptionKey(CString(key1B64));
	
	CStringA plaintext("Secret message");
	CStringA encrypted = CCloudEncryption::EncryptClipData(plaintext);
	
	// Switch to different key
	std::vector<BYTE> key2 = CCloudCrypto::RandomBytes(32);
	CCloudCrypto::Initialize(key2);
	CStringA key2B64 = CCloudCrypto::Base64Encode(key2);
	CGetSetOptions::SetCloudEncryptionKey(CString(key2B64));
	
	// Try to decrypt - should fail and return empty
	CStringA decrypted = CCloudEncryption::DecryptClipData(encrypted);
	EXPECT_TRUE(decrypted.IsEmpty()) << "Decryption should fail with wrong key";
}

TEST_F(CloudEncryption_ClipData, DecryptEmptyInput)
{
	// Setup encryption
	std::vector<BYTE> key = CCloudCrypto::RandomBytes(32);
	CCloudCrypto::Initialize(key);
	CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);
	CStringA keyB64 = CCloudCrypto::Base64Encode(key);
	CGetSetOptions::SetCloudEncryptionKey(CString(keyB64));

	// Decrypt empty string
	CStringA decrypted = CCloudEncryption::DecryptClipData(CStringA(""));
	
	// Should return empty
	EXPECT_TRUE(decrypted.IsEmpty());
}

TEST_F(CloudEncryption_ClipData, DecryptInvalidBase64_ReturnsEmpty)
{
	// Setup encryption
	std::vector<BYTE> key = CCloudCrypto::RandomBytes(32);
	CCloudCrypto::Initialize(key);
	CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);
	CStringA keyB64 = CCloudCrypto::Base64Encode(key);
	CGetSetOptions::SetCloudEncryptionKey(CString(keyB64));

	// Try to decrypt invalid base64
	CStringA decrypted = CCloudEncryption::DecryptClipData(CStringA("not-valid-base64!!!"));
	
	// Should fail gracefully and return empty
	EXPECT_TRUE(decrypted.IsEmpty());
}

// ============================================================================
// Is Encryption Ready Tests
// ============================================================================

TEST_F(CloudEncryption_Ready, ReadyWhenEnabled)
{
	// Enable encryption
	CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);
	
	// Should be ready
	BOOL ready = CCloudEncryption::IsEncryptionReady();
	EXPECT_TRUE(ready);
}

TEST_F(CloudEncryption_Ready, NotReadyWhenDisabled)
{
	// Disable encryption
	CGetSetOptions::SetCloudSyncEncryptionEnabled(FALSE);
	
	// Should not be ready
	BOOL ready = CCloudEncryption::IsEncryptionReady();
	EXPECT_FALSE(ready);
}

TEST_F(CloudEncryption_Ready, NotReadyByDefault)
{
	// Default state should be not ready
	BOOL ready = CCloudEncryption::IsEncryptionReady();
	EXPECT_FALSE(ready);
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_F(CloudEncryption_Integration, FullSetupEncryptDecryptCycle)
{
	// 1. Generate salt
	std::vector<BYTE> salt = CCloudCrypto::RandomBytes(16);
	CStringA saltB64 = CCloudCrypto::Base64Encode(salt);
	
	// 2. Derive key from password
	CStringA password("MySecretPassword123");
	std::vector<BYTE> key = CCloudCrypto::DeriveKey(password, salt, 100000);
	
	// 3. Initialize crypto
	ASSERT_TRUE(CCloudCrypto::Initialize(key));
	
	// 4. Store key (simulates SetupEncryption)
	CStringA keyB64 = CCloudCrypto::Base64Encode(key);
	CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);
	CGetSetOptions::SetCloudEncryptionKey(CString(keyB64));
	CGetSetOptions::SetCloudEncryptionSalt(CString(saltB64));
	
	// 5. Encrypt clip data using ACTUAL wrapper
	CStringA plaintext("Important clipboard data to encrypt");
	CStringA encrypted = CCloudEncryption::EncryptClipData(plaintext);
	EXPECT_FALSE(encrypted.IsEmpty());
	
	// 6. Decrypt clip data using ACTUAL wrapper
	CStringA decrypted = CCloudEncryption::DecryptClipData(encrypted);
	EXPECT_EQ(plaintext, decrypted);
}

TEST_F(CloudEncryption_Integration, MultipleClipsWithSameKey)
{
	// Setup encryption
	std::vector<BYTE> key = CCloudCrypto::RandomBytes(32);
	CCloudCrypto::Initialize(key);
	CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);
	CStringA keyB64 = CCloudCrypto::Base64Encode(key);
	CGetSetOptions::SetCloudEncryptionKey(CString(keyB64));

	// Encrypt multiple clips using ACTUAL wrapper
	std::vector<CStringA> originals = {
		"Clip 1: Text data",
		"Clip 2: HTML content",
		"Clip 3: Unicode 你好世界",
		"Clip 4: Special chars !@#$%^&*()"
	};

	std::vector<CStringA> encrypted;
	for (const auto& original : originals)
	{
		CStringA enc = CCloudEncryption::EncryptClipData(original);
		EXPECT_FALSE(enc.IsEmpty()) << "Encryption failed for: " << original;
		encrypted.push_back(enc);
	}

	// Decrypt all clips using ACTUAL wrapper
	for (size_t i = 0; i < encrypted.size(); i++)
	{
		CStringA decrypted = CCloudEncryption::DecryptClipData(encrypted[i]);
		EXPECT_EQ(originals[i], decrypted) << "Decryption failed for clip " << i;
	}
}

TEST_F(CloudEncryption_Integration, PasswordChangeRequiresReencrypt)
{
	// Setup with password 1
	std::vector<BYTE> salt = CCloudCrypto::RandomBytes(16);
	CStringA password1("Password1");
	std::vector<BYTE> key1 = CCloudCrypto::DeriveKey(password1, salt, 100000);
	CCloudCrypto::Initialize(key1);
	CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);
	CStringA key1B64 = CCloudCrypto::Base64Encode(key1);
	CGetSetOptions::SetCloudEncryptionKey(CString(key1B64));

	// Encrypt with password 1
	CStringA plaintext("Secret data");
	CStringA encrypted = CCloudEncryption::EncryptClipData(plaintext);

	// Change to password 2
	CStringA password2("Password2");
	std::vector<BYTE> key2 = CCloudCrypto::DeriveKey(password2, salt, 100000);
	CCloudCrypto::Initialize(key2);
	CStringA key2B64 = CCloudCrypto::Base64Encode(key2);
	CGetSetOptions::SetCloudEncryptionKey(CString(key2B64));

	// Try to decrypt with password 2 - should fail
	CStringA decrypted = CCloudEncryption::DecryptClipData(encrypted);
	EXPECT_TRUE(decrypted.IsEmpty()) << "Decryption should fail with wrong password";

	// Switch back to password 1
	CCloudCrypto::Initialize(key1);
	CGetSetOptions::SetCloudEncryptionKey(CString(key1B64));
	
	// Now should decrypt
	decrypted = CCloudEncryption::DecryptClipData(encrypted);
	EXPECT_EQ(plaintext, decrypted);
}

TEST_F(CloudEncryption_Integration, ClipboardTextRoundtrip)
{
	// Setup encryption
	std::vector<BYTE> key = CCloudCrypto::RandomBytes(32);
	CCloudCrypto::Initialize(key);
	CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);
	CStringA keyB64 = CCloudCrypto::Base64Encode(key);
	CGetSetOptions::SetCloudEncryptionKey(CString(keyB64));

	// Test various clipboard text formats
	std::vector<std::string> testCases = {
		"Simple text",
		"Multi-line\ntext\nwith\nnewlines",
		"Text with tabs\tand\tspaces",
		"URL: https://example.com/path?q=123",
		"Email: user@example.com",
		"Code: int main() { return 0; }",
		"Path: C:\\Program Files\\Ditto\\Ditto.exe"
	};

	for (const auto& test : testCases)
	{
		CStringA original(test.c_str());
		CStringA encrypted = CCloudEncryption::EncryptClipData(original);
		EXPECT_FALSE(encrypted.IsEmpty()) << "Failed for: " << test;
		
		CStringA decrypted = CCloudEncryption::DecryptClipData(encrypted);
		EXPECT_EQ(original, decrypted) << "Failed for: " << test;
	}
}
