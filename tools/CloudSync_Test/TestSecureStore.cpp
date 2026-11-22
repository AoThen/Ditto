#include <gtest/gtest.h>
#include "../../src/shared/SecureStore.h"

TEST(SecureStore, IsProtectedNullptr)
{
	EXPECT_FALSE(CSecureStore::IsProtected(nullptr));
}

TEST(SecureStore, IsProtectedEmptyString)
{
	EXPECT_FALSE(CSecureStore::IsProtected(_T("")));
}

TEST(SecureStore, IsProtectedPlainTextWithoutPrefix)
{
	EXPECT_FALSE(CSecureStore::IsProtected(_T("plain-secret")));
}

TEST(SecureStore, IsProtectedBarePrefix)
{
	EXPECT_TRUE(CSecureStore::IsProtected(_T("DPAPI:")));
}

TEST(SecureStore, IsProtectedPrefixedValue)
{
	EXPECT_TRUE(CSecureStore::IsProtected(_T("DPAPI:YWJj")));
}

TEST(SecureStore, ProtectEmptyInputReturnsEmpty)
{
	bool ok = false;
	CString result = CSecureStore::Protect(_T(""), &ok);
	EXPECT_TRUE(result.IsEmpty());
	EXPECT_TRUE(ok);
}

TEST(SecureStore, ProtectNullptrReturnsEmpty)
{
	bool ok = false;
	CString result = CSecureStore::Protect(nullptr, &ok);
	EXPECT_TRUE(result.IsEmpty());
	EXPECT_TRUE(ok);
}

TEST(SecureStore, ProtectReturnsPrefixedCipherText)
{
	bool ok = false;
	CString result = CSecureStore::Protect(_T("secret-value"), &ok);
	EXPECT_TRUE(ok);
	EXPECT_TRUE(CSecureStore::IsProtected(result));
	EXPECT_TRUE(result.GetLength() > 6);
	EXPECT_TRUE(result != CString(_T("secret-value")));
}

TEST(SecureStore, ProtectAllowsNullOkPointer)
{
	CString result = CSecureStore::Protect(_T("secret"), nullptr);
	EXPECT_TRUE(CSecureStore::IsProtected(result));
}

TEST(SecureStore, ProtectUnprotectRoundTrip)
{
	const TCHAR* samples[] = {
		_T("x"),
		_T("simple"),
		_T("with spaces and !@#$%^&*()"),
		_T("\x4E2D\x6587\x5BC6\x7801\x6D4B\x8BD5"), // Chinese text, encoding-independent
	};
	for (auto sample : samples)
	{
		CString protectedValue = CSecureStore::Protect(sample);
		EXPECT_TRUE(CSecureStore::IsProtected(protectedValue)) << "input: " << sample;

		CString plain = CSecureStore::Unprotect(protectedValue);
		EXPECT_TRUE(plain == CString(sample)) << "input: " << sample;
	}
}

TEST(SecureStore, UnprotectLegacyPlainTextPassThrough)
{
	EXPECT_TRUE(CSecureStore::Unprotect(_T("legacy-plain")) == CString(_T("legacy-plain")));
}

TEST(SecureStore, UnprotectNullptrReturnsEmpty)
{
	EXPECT_TRUE(CSecureStore::Unprotect(nullptr).IsEmpty());
}

TEST(SecureStore, UnprotectEmptyStringReturnsEmpty)
{
	EXPECT_TRUE(CSecureStore::Unprotect(_T("")).IsEmpty());
}

TEST(SecureStore, UnprotectInvalidBase64ReturnsEmpty)
{
	EXPECT_TRUE(CSecureStore::Unprotect(_T("DPAPI:!!!not-base64!!!")).IsEmpty());
}

TEST(SecureStore, UnprotectGarbageCipherTextReturnsEmpty)
{
	// Valid base64 (20 zero bytes) that is not a DPAPI blob must fail cleanly.
	EXPECT_TRUE(CSecureStore::Unprotect(_T("DPAPI:AAAAAAAAAAAAAAAAAAAAAA==")).IsEmpty());
}

// Step-by-step diagnosis of the DPAPI round trip; prints each stage to stdout
// so CI logs reveal exactly where the chain breaks.
TEST(SecureStore, DiagnosticDpapiSteps)
{
	CStringW src(L"x");
	DATA_BLOB in;
	in.pbData = (BYTE*)src.GetString();
	in.cbData = (int)(src.GetLength() * sizeof(WCHAR));

	DATA_BLOB encrypted;
	::SecureZeroMemory(&encrypted, sizeof(encrypted));
	BOOL protectOk = ::CryptProtectData(&in, L"diag", NULL, NULL, NULL, 0, &encrypted);
	printf("DIAG protect ok=%d cb=%d gle=%lu\n",
		protectOk, protectOk ? encrypted.cbData : -1, protectOk ? 0 : GetLastError());
	ASSERT_TRUE(protectOk);

	DWORD cchEncoded = 0;
	ASSERT_TRUE(::CryptBinaryToStringA(encrypted.pbData, encrypted.cbData,
		CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &cchEncoded));
	CStringA encodedA;
	ATLVERIFY(::CryptBinaryToStringA(encrypted.pbData, encrypted.cbData,
		CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, encodedA.GetBuffer(cchEncoded), &cchEncoded));
	encodedA.ReleaseBuffer(-1);
	printf("DIAG encode cch=%d\n", (int)cchEncoded);

	std::vector<BYTE> decoded;
	DWORD cbDecoded = 0;
	BOOL decodeOk = ::CryptStringToBinaryA(encodedA, encodedA.GetLength(), CRYPT_STRING_BASE64,
		NULL, &cbDecoded, NULL, NULL);
	if (decodeOk)
	{
		decoded.resize(cbDecoded);
		decodeOk = ::CryptStringToBinaryA(encodedA, encodedA.GetLength(), CRYPT_STRING_BASE64,
			&decoded[0], &cbDecoded, NULL, NULL);
	}
	printf("DIAG decode ok=%d cbActual=%d expected=%d gle=%lu\n",
		decodeOk, decodeOk ? (int)cbDecoded : -1, encrypted.cbData, decodeOk ? 0 : GetLastError());
	ASSERT_TRUE(decodeOk);
	ASSERT_EQ((int)encrypted.cbData, (int)cbDecoded);
	EXPECT_EQ(0, memcmp(encrypted.pbData, &decoded[0], encrypted.cbData));

	DATA_BLOB cipher;
	cipher.pbData = &decoded[0];
	cipher.cbData = (DWORD)cbDecoded;
	DATA_BLOB plain;
	::SecureZeroMemory(&plain, sizeof(plain));
	BOOL unprotectOk = ::CryptUnprotectData(&cipher, NULL, NULL, NULL, NULL, 0, &plain);
	printf("DIAG unprotect ok=%d cb=%d gle=%lu\n",
		unprotectOk, unprotectOk ? plain.cbData : -1, unprotectOk ? 0 : GetLastError());
	ASSERT_TRUE(unprotectOk);
	ASSERT_EQ((int)in.cbData, (int)plain.cbData);
	EXPECT_EQ(0, memcmp(in.pbData, plain.pbData, in.cbData));

	::LocalFree(encrypted.pbData);
	::LocalFree(plain.pbData);
}
