#include <gtest/gtest.h>
#include "../src/Md5.h"
#include <cstring>

TEST(Md5, EmptyString)
{
	CMd5 md5;
	char* result = md5.CalcMD5FromString("", 0);
	EXPECT_STREQ(result, "d41d8cd98f00b204e9800998ecf8427e");
	md5.FreeBuffer();
}

TEST(Md5, Hello)
{
	CMd5 md5;
	char* result = md5.CalcMD5FromString("hello", 5);
	EXPECT_STREQ(result, "5d41402abc4b2a76b9719d911017c592");
	md5.FreeBuffer();
}

TEST(Md5, QuickBrownFox)
{
	CMd5 md5;
	const char* input = "The quick brown fox jumps over the lazy dog";
	char* result = md5.CalcMD5FromString(input, (int)strlen(input));
	EXPECT_STREQ(result, "9e107d9d372bb6826bd81d3542a419d6");
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