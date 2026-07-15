#include "gtest/gtest.h"
#include "../EncryptDecrypt/Encryption.h"
#include <cstring>

TEST(Encryption, EncryptDecryptRoundTrip)
{
    CEncryption enc;
    const char* plaintext = "Hello";
    unsigned char* encrypted = nullptr;
    int encLen = 0;
    unsigned char* decrypted = nullptr;
    int decLen = 0;

    EXPECT_TRUE(enc.Encrypt(
        (const unsigned char*)plaintext, (int)strlen(plaintext),
        "test123", encrypted, encLen));

    EXPECT_TRUE(enc.Decrypt(
        encrypted, encLen, "test123", decrypted, decLen));

    ASSERT_EQ(decLen, (int)strlen(plaintext));
    EXPECT_EQ(memcmp(decrypted, plaintext, decLen), 0);

    enc.FreeBuffer(encrypted);
    enc.FreeBuffer(decrypted);
}

TEST(Encryption, EmptyInputNoCrash)
{
    CEncryption enc;
    unsigned char* encrypted = nullptr;
    int encLen = 0;
    unsigned char* decrypted = nullptr;
    int decLen = 0;

    EXPECT_TRUE(enc.Encrypt(
        (const unsigned char*)"", 0,
        "test123", encrypted, encLen));

    EXPECT_TRUE(enc.Decrypt(
        encrypted, encLen, "test123", decrypted, decLen));

    ASSERT_EQ(decLen, 0);

    enc.FreeBuffer(encrypted);
    enc.FreeBuffer(decrypted);
}

TEST(Encryption, DifferentPasswordDifferentCiphertext)
{
    CEncryption enc;
    const char* plaintext = "Hello";
    unsigned char* encrypted1 = nullptr;
    int encLen1 = 0;
    unsigned char* encrypted2 = nullptr;
    int encLen2 = 0;

    EXPECT_TRUE(enc.Encrypt(
        (const unsigned char*)plaintext, (int)strlen(plaintext),
        "password1", encrypted1, encLen1));

    EXPECT_TRUE(enc.Encrypt(
        (const unsigned char*)plaintext, (int)strlen(plaintext),
        "password2", encrypted2, encLen2));

    ASSERT_EQ(encLen1, encLen2);
    EXPECT_NE(memcmp(encrypted1, encrypted2, encLen1), 0);

    enc.FreeBuffer(encrypted1);
    enc.FreeBuffer(encrypted2);
}
