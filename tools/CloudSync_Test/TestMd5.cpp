#include <gtest/gtest.h>
#include "../../src/Md5.h"
#include <cstring>

TEST(Md5, EmptyString)
{
	CMd5 md5;
	char* result = md5.CalcMD5FromString("", 0);
	EXPECT_STREQ(result, "D41D8CD98F00B204E9800998ECF8427E"); // CMd5::CalcMD5FromString outputs uppercase hex
	md5.FreeBuffer();
}

TEST(Md5, Hello)
{
	CMd5 md5;
	char* result = md5.CalcMD5FromString("hello", 5);
	EXPECT_STREQ(result, "5D41402ABC4B2A76B9719D911017C592"); // uppercase hex
	md5.FreeBuffer();
}

TEST(Md5, QuickBrownFox)
{
	CMd5 md5;
	const char* input = "The quick brown fox jumps over the lazy dog";
	char* result = md5.CalcMD5FromString(input, (int)strlen(input));
	EXPECT_STREQ(result, "9E107D9D372BB6826BD81D3542A419D6"); // uppercase hex
	md5.FreeBuffer();
}

TEST(Md5, LargeData)
{
	CMd5 md5;
	char data[1024];
	for (int i = 0; i < 1024; i++)
		data[i] = static_cast<char>(i & 0xFF);
	char* result = md5.CalcMD5FromString(data, 1024);
	ASSERT_NE(result, nullptr);
	EXPECT_EQ(strlen(result), 32u);
	md5.FreeBuffer();
}