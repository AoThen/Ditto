#include <gtest/gtest.h>
#include "../../src/WildCardMatch.h"

TEST(WildCardMatch, ExactMatch)
{
	EXPECT_TRUE(CWildCardMatch::WildMatch(L"hello", L"hello", L""));
}

TEST(WildCardMatch, StarMatchesAny)
{
	EXPECT_TRUE(CWildCardMatch::WildMatch(L"*world", L"hello world", L""));
}

TEST(WildCardMatch, StarNotMatch)
{
	EXPECT_FALSE(CWildCardMatch::WildMatch(L"hello", L"world", L""));
}

TEST(WildCardMatch, QuestionMarkMatchesSingle)
{
	EXPECT_TRUE(CWildCardMatch::WildMatch(L"a?cd", L"abcd", L""));
}

TEST(WildCardMatch, EmptyPattern)
{
	EXPECT_TRUE(CWildCardMatch::WildMatch(L"", L"", L""));
}

TEST(WildCardMatch, ComplexPattern)
{
	EXPECT_TRUE(CWildCardMatch::WildMatch(L"*.txt", L"test.txt", L""));
}

TEST(WildCardMatch, LimitCharParamNoLimit)
{
	// 3rd param sLimitChar is a whitelist of chars allowed to be matched by '*'.
	// Empty string = no restriction, so "*.txt" matches "test.txt".
	EXPECT_TRUE(CWildCardMatch::WildMatch(L"*.txt", L"test.txt", L""));
}