#include <gtest/gtest.h>
#include "../OcrCli/OcrCliUtil.h"

TEST(OcrCliUtil, EscapeJsonPlainTextUnchanged)
{
	EXPECT_EQ("plain text 123", EscapeJson("plain text 123"));
}

TEST(OcrCliUtil, EscapeJsonQuoteAndBackslash)
{
	EXPECT_EQ("a\\\"b\\\\c", EscapeJson("a\"b\\c"));
}

TEST(OcrCliUtil, EscapeJsonNewlineCarriageReturnTab)
{
	EXPECT_EQ("l1\\nl2\\rt\\tend", EscapeJson("l1\nl2\rt\tend"));
}

TEST(OcrCliUtil, EscapeJsonControlBytesUseUnicodeEscape)
{
	EXPECT_EQ("\\u0001", EscapeJson(std::string(1, '\x01')));
	EXPECT_EQ("\\u001f", EscapeJson(std::string(1, '\x1f')));
	EXPECT_EQ("a\\u0007b", EscapeJson(std::string("a\x07" "b")));
}

TEST(OcrCliUtil, EscapeJsonMixedContent)
{
	std::string input = "line1\nline2 \"quoted\" end";
	std::string expected = "line1\\nline2 \\\"quoted\\\" end";
	EXPECT_EQ(expected, EscapeJson(input));
}

TEST(OcrCliUtil, EscapeJsonEmptyString)
{
	EXPECT_EQ("", EscapeJson(""));
}

TEST(OcrCliUtil, WStringToUtf8Ascii)
{
	EXPECT_EQ("hello", WStringToUtf8(L"hello"));
}

TEST(OcrCliUtil, WStringToUtf8Chinese)
{
	// "中文" in UTF-8: E4 B8 AD E6 96 87
	std::string utf8 = WStringToUtf8(L"\x4E2D\x6587");
	EXPECT_EQ(std::string("\xE4\xB8\xAD\xE6\x96\x87"), utf8);
}

TEST(OcrCliUtil, WStringToUtf8Empty)
{
	EXPECT_EQ("", WStringToUtf8(L""));
}
