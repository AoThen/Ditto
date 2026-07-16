#include <gtest/gtest.h>
#include "../src/RegExFilterHelper.h"

TEST(RegExFilter, ExactMatch)
{
	CRegExFilterData filter;
	filter.m_regEx = L"hello";
	filter.ParseFilters();
	std::wstring text = L"hello";
	EXPECT_TRUE(filter.MatchesRegEx(text));
}

TEST(RegExFilter, PartialMatch)
{
	CRegExFilterData filter;
	filter.m_regEx = L"world";
	filter.ParseFilters();
	std::wstring text = L"hello world";
	EXPECT_TRUE(filter.MatchesRegEx(text));
}

TEST(RegExFilter, NoMatch)
{
	CRegExFilterData filter;
	filter.m_regEx = L"xyz";
	filter.ParseFilters();
	std::wstring text = L"hello";
	EXPECT_FALSE(filter.MatchesRegEx(text));
}

TEST(RegExFilter, SpecialChars)
{
	CRegExFilterData filter;
	filter.m_regEx = L"hello\\.world";
	filter.ParseFilters();
	std::wstring text = L"hello.world";
	EXPECT_TRUE(filter.MatchesRegEx(text));
}

TEST(RegExFilter, EmptyPattern)
{
	// Empty pattern means no regex configured => MatchesRegEx returns false (design contract).
	CRegExFilterData filter;
	filter.m_regEx = L"";
	filter.ParseFilters();
	std::wstring text = L"";
	EXPECT_FALSE(filter.MatchesRegEx(text));
}