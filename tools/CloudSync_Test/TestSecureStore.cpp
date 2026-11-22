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
		_T("中文密码测试"),
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
