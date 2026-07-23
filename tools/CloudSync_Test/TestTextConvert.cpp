#include <gtest/gtest.h>
#include "../Addins/DittoUtil/TextConvert.h"

TEST(TextConvert, Utf8RoundTrip)
{
	CStringA utf8 = "hello";
	CString wide;
	CStringA result;
	EXPECT_TRUE(CTextConvert::ConvertFromUTF8(utf8, wide));
	EXPECT_TRUE(CTextConvert::ConvertToUTF8(wide, result));
	EXPECT_STREQ(utf8, result);
}

TEST(TextConvert, ChineseRoundTrip)
{
	CStringA utf8 = "\xe4\xbd\xa0\xe5\xa5\xbd";
	CString wide;
	CStringA result;
	EXPECT_TRUE(CTextConvert::ConvertFromUTF8(utf8, wide));
	EXPECT_TRUE(CTextConvert::ConvertToUTF8(wide, result));
	EXPECT_STREQ(utf8, result);
}

TEST(TextConvert, EmptyString)
{
	CStringA utf8;
	CString wide;
	CStringA result;
	EXPECT_TRUE(CTextConvert::ConvertFromUTF8(utf8, wide));
	EXPECT_TRUE(wide.IsEmpty());
	EXPECT_TRUE(CTextConvert::ConvertToUTF8(wide, result));
	EXPECT_TRUE(result.IsEmpty());
}

TEST(TextConvert, MultiByteUnicodeRoundTrip)
{
	CStringA mb = "test";
	CStringW unicode = CTextConvert::MultiByteToUnicodeString(mb);
	CStringA result = CTextConvert::UnicodeStringToMultiByte(unicode);
	EXPECT_STREQ(mb, result);
}

TEST(TextConvert, ConvertToCharAndUnicode)
{
	CStringW wide = L"test";
	CStringA resultA = CTextConvert::ConvertToChar(wide);
	CStringW resultW = CTextConvert::ConvertToUnicode(wide);
	EXPECT_STREQ(resultW, wide);
}