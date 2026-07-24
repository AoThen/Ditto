#include <gtest/gtest.h>
#include "../../src/ClipCompare.h"
#include <cstring>

static bool IsClipDataDifferent(const char* data1, size_t len1, const char* data2, size_t len2)
{
    if (len1 != len2) return true;
    return memcmp(data1, data2, len1) != 0;
}

static bool IsClipDataDifferent(const char* text1, const char* text2)
{
    if (text1 == nullptr && text2 == nullptr) return false;
    if (text1 == nullptr || text2 == nullptr) return true;
    return strcmp(text1, text2) != 0;
}

TEST(ClipCompare, ConstructAndDestroy) {
	CClipCompare compare;
	SUCCEED();
}

TEST(ClipCompare, CompareTextData_Identical) {
	EXPECT_FALSE(IsClipDataDifferent("hello", "hello"));
	EXPECT_FALSE(IsClipDataDifferent("", ""));
	EXPECT_FALSE(IsClipDataDifferent(nullptr, nullptr));
}

TEST(ClipCompare, CompareTextData_Different) {
	EXPECT_TRUE(IsClipDataDifferent("hello", "world"));
	EXPECT_TRUE(IsClipDataDifferent("abc", "abcd"));
	EXPECT_TRUE(IsClipDataDifferent("Hello", "hello"));
}

TEST(ClipCompare, CompareTextData_EmptyVsNonEmpty) {
	EXPECT_TRUE(IsClipDataDifferent("", "nonempty"));
	EXPECT_TRUE(IsClipDataDifferent("nonempty", ""));
	EXPECT_TRUE(IsClipDataDifferent(nullptr, "something"));
	EXPECT_TRUE(IsClipDataDifferent("something", nullptr));
}

TEST(ClipCompare, CompareBinaryData_LengthMismatch) {
	const char a[] = { 'A', 'B', 'C', 0 };
	const char b[] = { 'A', 'B', 'C', 0, 'D' };
	EXPECT_TRUE(IsClipDataDifferent(a, sizeof(a), b, sizeof(b)));
}

TEST(ClipCompare, CompareBinaryData_IdenticalBinary) {
	const unsigned char a[] = { 0x00, 0x01, 0x02, 0xFF };
	const unsigned char b[] = { 0x00, 0x01, 0x02, 0xFF };
	EXPECT_FALSE(IsClipDataDifferent(reinterpret_cast<const char*>(a), sizeof(a), reinterpret_cast<const char*>(b), sizeof(b)));
}
