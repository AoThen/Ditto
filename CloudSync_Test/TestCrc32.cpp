#include <gtest/gtest.h>
#include "../src/Crc32Dynamic.h"

TEST(Crc32, EmptyData)
{
	CCrc32Dynamic crc;
	DWORD result = 0;
	crc.GenerateCrc32(nullptr, 0, result);
	EXPECT_EQ(result, 0u);
}

TEST(Crc32, KnownVector)
{
	CCrc32Dynamic crc;
	BYTE data[] = { 'h','e','l','l','o',0 };
	DWORD result = 0;
	crc.GenerateCrc32(data, 5, result);
	EXPECT_EQ(result, 0x3610A686u);
}

TEST(Crc32, LargeData)
{
	CCrc32Dynamic crc;
	BYTE data[1024];
	for (int i = 0; i < 1024; i++)
		data[i] = static_cast<BYTE>(i & 0xFF);
	DWORD result = 0;
	crc.GenerateCrc32(data, 1024, result);
	EXPECT_NE(result, 0u);
}