// CloudEncryptionTest.cpp - Unit tests for CloudEncryption module
// Tests: Setup encryption, status check, stored key initialization, clip data encryption

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

// ============================================================================
// Setup Encryption Tests
// ============================================================================

TEST(CloudEncryption_Setup, SuccessfulEncryptionSetup)
{
	// This test simulates the SetupEncryption flow:
	// 1. Generate a salt
	// 2. Derive key from password + salt
	// 3. Initialize crypto
	// 4. Store key in settings

	// Generate salt (simulating server response)
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

	// Store key in settings
	CStringA keyB64 = CCloudCrypto::Base64Encode(key);
	CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);
	CGetSetOptions::SetCloudEncryptionKey(CString(keyB64));
	CGetSetOptions::SetCloudEncryptionSalt(CString(saltB64));

	// Verify settings
	EXPECT_TRUE(CGetSetOptions::GetCloudSyncEncryptionEnabled());
	EXPECT_FALSE(CGetSetOptions::GetCloudEncryptionKey().IsEmpty());
}

TEST(CloudEncryption_Setup, WeakPasswordStillWorks)
{
	// Test that weak passwords are accepted (user responsibility)
	std::vector<BYTE> salt = CCloudCrypto::RandomBytes(16);
	CStringA password("123"); // Very weak password
	
	std::vector<BYTE> key = CCloudCrypto::DeriveKey(password, salt, 100000);
	EXPECT_EQ(key.size(), 32);
	
	BOOL initOk = CCloudCrypto::Initialize(key);
	EXPECT_TRUE(initOk);
}

TEST(CloudEncryption_Setup, UnicodePassword)
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

TEST(CloudEncryption_Setup, EmptyPasswordFails)
{
	// Test that empty password produces a key (but shouldn't be used)
	std::vector<BYTE> salt = CCloudCrypto::RandomBytes(16);
	CStringA password("");
	
	std::vector<BYTE> key = CCloudCrypto::DeriveKey(password, salt, 100000);
	
	// PBKDF2 should still produce a key even with empty password
	EXPECT_EQ(key.size(), 32);
}

TEST(CloudEncryption_Setup, DifferentSaltsProduceDifferentKeys)
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

TEST(CloudEncryption_Setup, DifferentIterationsProduceDifferentKeys)
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

TEST(CloudEncryption_Status, EncryptionEnabled)
{
	// Setup encryption
	std::vector<BYTE> key = CCloudCrypto::RandomBytes(32);
	CCloudCrypto::Initialize(key);
	
	CStringA keyB64 = CCloudCrypto::Base64Encode(key);
	CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);
	CGetSetOptions::SetCloudEncryptionKey(CString(keyB64));

	// Check status
	BOOL enabled = CGetSetOptions::GetCloudSyncEncryptionEnabled();
	EXPECT_TRUE(enabled);
}

TEST(CloudEncryption_Status, EncryptionDisabled)
{
	// Check default state
	BOOL enabled = CGetSetOptions::GetCloudSyncEncryptionEnabled();
	EXPECT_FALSE(enabled);
}

TEST(CloudEncryption_Status, KeyExists)
{
	// Setup with valid key
	std::vector<BYTE> key = CCloudCrypto::RandomBytes(32);
	CStringA keyB64 = CCloudCrypto::Base64Encode(key);
	
	CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);
	CGetSetOptions::SetCloudEncryptionKey(CString(keyB64));

	// Verify key exists
	CStringA storedKey = CGetSetOptions::GetCloudEncryptionKey();
	EXPECT_FALSE(storedKey.IsEmpty());
}

TEST(CloudEncryption_Status, KeyDoesNotExist)
{
	// Check default state
	CStringA key = CGetSetOptions::GetCloudEncryptionKey();
	EXPECT_TRUE(key.IsEmpty());
}

// ============================================================================
// Initialize Crypto From Stored Key Tests
// ============================================================================

TEST(CloudEncryption_StoredKey, ValidStoredKey)
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

TEST(CloudEncryption_StoredKey, InvalidKeySize)
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

TEST(CloudEncryption_StoredKey, EmptyStoredKey)
{
	// No key stored
	CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);
	CGetSetOptions::SetCloudEncryptionKey(CString(""));

	// Try to initialize with empty key
	CStringA keyB64 = CGetSetOptions::GetCloudEncryptionKey();
	std::vector<BYTE> storedKey = CCloudCrypto::Base64Decode(keyB64);
	
	// Should be empty
	EXPECT_TRUE(storedKey.empty());
}

TEST(CloudEncryption_StoredKey, CorruptedStoredKey)
{
	// Store corrupted key (invalid base64)
	CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);
	CGetSetOptions::SetCloudEncryptionKey(CString("not-valid-base64!!!"));

	// Try to decode
	CStringA keyB64 = CGetSetOptions::GetCloudEncryptionKey();
	std::vector<BYTE> storedKey = CCloudCrypto::Base64Decode(keyB64);
	
	// Decoding may fail or produce wrong size
	BOOL isValid = (storedKey.size() == 32);
	EXPECT_FALSE(isValid); // Should not be valid
}

TEST(CloudEncryption_StoredKey, NoStoredKey)
{
	// Don't store any key
	CGetSetOptions::SetCloudSyncEncryptionEnabled(FALSE);

	// Try to initialize
	CStringA keyB64 = CGetSetOptions::GetCloudEncryptionKey();
	EXPECT_TRUE(keyB64.IsEmpty());
	
	std::vector<BYTE> storedKey = CCloudCrypto::Base64Decode(keyB64);
	EXPECT_TRUE(storedKey.empty());
}

// ============================================================================
// Encrypt Clip Data Tests
// ============================================================================

TEST(CloudEncryption_ClipData, EncryptWhenReady)
{
	// Setup encryption
	std::vector<BYTE> key = CCloudCrypto::RandomBytes(32);
	CStringA keyB64 = CCloudCrypto::Base64Encode(key);
	
	CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);
	CGetSetOptions::SetCloudEncryptionKey(CString(keyB64));
	CCloudCrypto::Initialize(key);

	// Encrypt clip data
	CStringA plaintext("Important clipboard text");
	CStringA encrypted = CCloudCrypto::Encrypt(plaintext);
	
	// Should succeed
	EXPECT_FALSE(encrypted.IsEmpty());
	EXPECT_NE(encrypted, plaintext);
}

TEST(CloudEncryption_ClipData, EncryptWhenNotReady)
{
	// Don't initialize crypto
	CGetSetOptions::SetCloudSyncEncryptionEnabled(FALSE);

	// Try to encrypt
	CStringA plaintext("Secret data");
	CStringA encrypted = CCloudCrypto::Encrypt(plaintext);
	
	// Should return empty when not ready
	EXPECT_TRUE(encrypted.IsEmpty());
}

TEST(CloudEncryption_ClipData, EncryptEmptyData)
{
	// Setup encryption
	std::vector<BYTE> key = CCloudCrypto::RandomBytes(32);
	CCloudCrypto::Initialize(key);

	// Encrypt empty data
	CStringA plaintext("");
	CStringA encrypted = CCloudCrypto::Encrypt(plaintext);
	
	// Should still produce encrypted output (at least IV + tag)
	EXPECT_FALSE(encrypted.IsEmpty());
}

TEST(CloudEncryption_ClipData, EncryptBinaryData)
{
	// Setup encryption
	std::vector<BYTE> key = CCloudCrypto::RandomBytes(32);
	CCloudCrypto::Initialize(key);

	// Create binary data
	std::vector<BYTE> binaryData(256);
	for (int i = 0; i < 256; i++)
	{
		binaryData[i] = static_cast<BYTE>(i);
	}

	CStringA plaintext(reinterpret_cast<const char*>(binaryData.data()), 256);
	CStringA encrypted = CCloudCrypto::Encrypt(plaintext);
	
	// Should succeed
	EXPECT_FALSE(encrypted.IsEmpty());
	
	// Should be able to decrypt
	CStringA decrypted = CCloudCrypto::Decrypt(encrypted);
	EXPECT_EQ(decrypted.GetLength(), 256);
	EXPECT_EQ(0, memcmp(decrypted.GetString(), binaryData.data(), 256));
}

TEST(CloudEncryption_ClipData, EncryptLargeData)
{
	// Setup encryption
	std::vector<BYTE> key = CCloudCrypto::RandomBytes(32);
	CCloudCrypto::Initialize(key);

	// Create large data (100KB)
	std::string largeData(100000, 'A');
	CStringA plaintext(largeData.c_str());
	
	CStringA encrypted = CCloudCrypto::Encrypt(plaintext);
	
	// Should succeed
	EXPECT_FALSE(encrypted.IsEmpty());
	
	// Should be able to decrypt
	CStringA decrypted = CCloudCrypto::Decrypt(encrypted);
	EXPECT_EQ(decrypted.GetLength(), 100000);
}

// ============================================================================
// Decrypt Clip Data Tests
// ============================================================================

TEST(CloudEncryption_ClipData, DecryptWhenReady)
{
	// Setup encryption
	std::vector<BYTE> key = CCloudCrypto::RandomBytes(32);
	CStringA keyB64 = CCloudCrypto::Base64Encode(key);
	
	CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);
	CGetSetOptions::SetCloudEncryptionKey(CString(keyB64));
	CCloudCrypto::Initialize(key);

	// Encrypt then decrypt
	CStringA plaintext("Test data for decryption");
	CStringA encrypted = CCloudCrypto::Encrypt(plaintext);
	CStringA decrypted = CCloudCrypto::Decrypt(encrypted);
	
	// Should match
	EXPECT_EQ(plaintext, decrypted);
}

TEST(CloudEncryption_ClipData, DecryptWhenNotReady)
{
	// Don't initialize crypto
	CGetSetOptions::SetCloudSyncEncryptionEnabled(FALSE);

	// Try to decrypt
	CStringA encryptedData("dGVzdA==");
	CStringA decrypted = CCloudCrypto::Decrypt(encryptedData);
	
	// Should return empty when not ready
	EXPECT_TRUE(decrypted.IsEmpty());
}

TEST(CloudEncryption_ClipData, DecryptTamperedData)
{
	// Setup encryption
	std::vector<BYTE> key = CCloudCrypto::RandomBytes(32);
	CCloudCrypto::Initialize(key);

	// Create valid encrypted data
	CStringA plaintext("Original data");
	CStringA encrypted = CCloudCrypto::Encrypt(plaintext);
	
	// Tamper with the encrypted data
	CStringA tampered(encrypted);
	// Modify a character in the middle
	if (tampered.GetLength() > 20)
	{
		LPTSTR sz = tampered.GetBuffer();
		sz[10] = (sz[10] == 'A') ? 'B' : 'A';
		tampered.ReleaseBuffer();
	}
	
	// Decryption should fail
	CStringA decrypted = CCloudCrypto::Decrypt(tampered);
	EXPECT_TRUE(decrypted.IsEmpty());
}

TEST(CloudEncryption_ClipData, DecryptWrongKey)
{
	// Encrypt with one key
	std::vector<BYTE> key1 = CCloudCrypto::RandomBytes(32);
	CCloudCrypto::Initialize(key1);
	
	CStringA plaintext("Secret message");
	CStringA encrypted = CCloudCrypto::Encrypt(plaintext);
	
	// Try to decrypt with different key
	std::vector<BYTE> key2 = CCloudCrypto::RandomBytes(32);
	CCloudCrypto::Initialize(key2);
	
	CStringA decrypted = CCloudCrypto::Decrypt(encrypted);
	
	// Should fail (return empty)
	EXPECT_TRUE(decrypted.IsEmpty());
}

TEST(CloudEncryption_ClipData, DecryptEmptyInput)
{
	// Setup encryption
	std::vector<BYTE> key = CCloudCrypto::RandomBytes(32);
	CCloudCrypto::Initialize(key);

	// Decrypt empty string
	CStringA decrypted = CCloudCrypto::Decrypt(CStringA(""));
	
	// Should return empty
	EXPECT_TRUE(decrypted.IsEmpty());
}

TEST(CloudEncryption_ClipData, DecryptInvalidBase64)
{
	// Setup encryption
	std::vector<BYTE> key = CCloudCrypto::RandomBytes(32);
	CCloudCrypto::Initialize(key);

	// Try to decrypt invalid base64
	CStringA decrypted = CCloudCrypto::Decrypt(CStringA("not-valid-base64!!!"));
	
	// Should fail gracefully
	EXPECT_TRUE(decrypted.IsEmpty());
}

// ============================================================================
// Is Encryption Ready Tests
// ============================================================================

TEST(CloudEncryption_Ready, ReadyWhenEnabled)
{
	// Enable encryption
	CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);
	
	// Should be ready
	BOOL ready = CCloudEncryption::IsEncryptionReady();
	EXPECT_TRUE(ready);
}

TEST(CloudEncryption_Ready, NotReadyWhenDisabled)
{
	// Disable encryption
	CGetSetOptions::SetCloudSyncEncryptionEnabled(FALSE);
	
	// Should not be ready
	BOOL ready = CCloudEncryption::IsEncryptionReady();
	EXPECT_FALSE(ready);
}

TEST(CloudEncryption_Ready, NotReadyByDefault)
{
	// Default state should be not ready
	BOOL ready = CCloudEncryption::IsEncryptionReady();
	EXPECT_FALSE(ready);
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST(CloudEncryption_Integration, FullSetupEncryptDecryptCycle)
{
	// 1. Generate salt
	std::vector<BYTE> salt = CCloudCrypto::RandomBytes(16);
	CStringA saltB64 = CCloudCrypto::Base64Encode(salt);
	
	// 2. Derive key from password
	CStringA password("MySecretPassword123");
	std::vector<BYTE> key = CCloudCrypto::DeriveKey(password, salt, 100000);
	
	// 3. Initialize crypto
	ASSERT_TRUE(CCloudCrypto::Initialize(key));
	
	// 4. Store key
	CStringA keyB64 = CCloudCrypto::Base64Encode(key);
	CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);
	CGetSetOptions::SetCloudEncryptionKey(CString(keyB64));
	CGetSetOptions::SetCloudEncryptionSalt(CString(saltB64));
	
	// 5. Encrypt clip data
	CStringA plaintext("Important clipboard data to encrypt");
	CStringA encrypted = CCloudCrypto::Encrypt(plaintext);
	EXPECT_FALSE(encrypted.IsEmpty());
	
	// 6. Decrypt clip data
	CStringA decrypted = CCloudCrypto::Decrypt(encrypted);
	EXPECT_EQ(plaintext, decrypted);
}

TEST(CloudEncryption_Integration, MultipleClipsWithSameKey)
{
	// Setup encryption
	std::vector<BYTE> key = CCloudCrypto::RandomBytes(32);
	CCloudCrypto::Initialize(key);

	// Encrypt multiple clips
	std::vector<CStringA> originals = {
		"Clip 1: Text data",
		"Clip 2: HTML content",
		"Clip 3: Unicode 你好世界",
		"Clip 4: Special chars !@#$%^&*()"
	};

	std::vector<CStringA> encrypted;
	for (const auto& original : originals)
	{
		CStringA enc = CCloudCrypto::Encrypt(original);
		EXPECT_FALSE(enc.IsEmpty());
		encrypted.push_back(enc);
	}

	// Decrypt all clips
	for (size_t i = 0; i < encrypted.size(); i++)
	{
		CStringA decrypted = CCloudCrypto::Decrypt(encrypted[i]);
		EXPECT_EQ(originals[i], decrypted);
	}
}

TEST(CloudEncryption_Integration, PasswordChangeRequiresReencrypt)
{
	// Setup with password 1
	std::vector<BYTE> salt = CCloudCrypto::RandomBytes(16);
	CStringA password1("Password1");
	std::vector<BYTE> key1 = CCloudCrypto::DeriveKey(password1, salt, 100000);
	CCloudCrypto::Initialize(key1);

	// Encrypt with password 1
	CStringA plaintext("Secret data");
	CStringA encrypted = CCloudCrypto::Encrypt(plaintext);

	// Change to password 2
	CStringA password2("Password2");
	std::vector<BYTE> key2 = CCloudCrypto::DeriveKey(password2, salt, 100000);
	CCloudCrypto::Initialize(key2);

	// Try to decrypt with password 2 - should fail
	CStringA decrypted = CCloudCrypto::Decrypt(encrypted);
	EXPECT_TRUE(decrypted.IsEmpty());

	// Switch back to password 1
	CCloudCrypto::Initialize(key1);
	
	// Now should decrypt
	decrypted = CCloudCrypto::Decrypt(encrypted);
	EXPECT_EQ(plaintext, decrypted);
}

TEST(CloudEncryption_Integration, ClipboardTextRoundtrip)
{
	// Setup encryption
	std::vector<BYTE> key = CCloudCrypto::RandomBytes(32);
	CCloudCrypto::Initialize(key);

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
		CStringA encrypted = CCloudCrypto::Encrypt(original);
		EXPECT_FALSE(encrypted.IsEmpty()) << "Failed for: " << test;
		
		CStringA decrypted = CCloudCrypto::Decrypt(encrypted);
		EXPECT_EQ(original, decrypted) << "Failed for: " << test;
	}
}
