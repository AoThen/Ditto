#include <gtest/gtest.h>
#include "../src/Slugify.h"

TEST(Slugify, BasicEnglish)
{
	EXPECT_EQ(slugify(L"Hello World", L"-"), L"hello-world");
}

TEST(Slugify, SpecialCharsRemoved)
{
	EXPECT_EQ(slugify(L"Hello! World?", L"-"), L"hello-world");
}

TEST(Slugify, EmptyInput)
{
	EXPECT_EQ(slugify(L"", L"-"), L"");
}

TEST(Slugify, MixedContent)
{
	EXPECT_EQ(slugify(L"Test 123", L"-"), L"test-123");
}

TEST(Slugify, CustomSeparator)
{
	EXPECT_EQ(slugify(L"Hello World", L"_"), L"hello_world");
}
