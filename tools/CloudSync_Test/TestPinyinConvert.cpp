#include <gtest/gtest.h>
#include "../src/Pinyin_Convert.h"

TEST(PinyinConvert, ExtractAlphaPureLetters)
{
	CPinyinConvert conv;
	EXPECT_TRUE(conv.ExtractAlpha(_T("nihao")) == _T("nihao"));
}

TEST(PinyinConvert, ExtractAlphaMixedCase)
{
	CPinyinConvert conv;
	EXPECT_TRUE(conv.ExtractAlpha(_T("NiHao")) == _T("NiHao"));
}

TEST(PinyinConvert, ExtractAlphaRemovesSpace)
{
	CPinyinConvert conv;
	EXPECT_TRUE(conv.ExtractAlpha(_T("ni hao")) == _T("nihao"));
}

TEST(PinyinConvert, ExtractAlphaKeepsOnlyLettersWithChinese)
{
	CPinyinConvert conv;
	EXPECT_TRUE(conv.ExtractAlpha(_T("ni好")) == _T("ni"));
}

TEST(PinyinConvert, ExtractAlphaDigitsReturnEmpty)
{
	CPinyinConvert conv;
	EXPECT_TRUE(conv.ExtractAlpha(_T("123")) == _T(""));
}

TEST(PinyinConvert, ExtractAlphaEmptyInput)
{
	CPinyinConvert conv;
	EXPECT_TRUE(conv.ExtractAlpha(_T("")) == _T(""));
}

TEST(PinyinConvert, IsAlphaQueryPureLetters)
{
	CPinyinConvert conv;
	EXPECT_TRUE(conv.IsAlphaQuery(std::wstring(_T("nihao"))) == true);
}

TEST(PinyinConvert, IsAlphaQueryWithSpace)
{
	CPinyinConvert conv;
	EXPECT_TRUE(conv.IsAlphaQuery(std::wstring(_T("ni hao"))) == false);
}

TEST(PinyinConvert, IsAlphaQueryWithChinese)
{
	CPinyinConvert conv;
	EXPECT_TRUE(conv.IsAlphaQuery(std::wstring(_T("ni好"))) == false);
}

TEST(PinyinConvert, IsAlphaQueryEmpty)
{
	CPinyinConvert conv;
	EXPECT_TRUE(conv.IsAlphaQuery(std::wstring(_T(""))) == false);
}

TEST(PinyinConvert, TextToPinyinChinese)
{
	auto py = CPinyinConvert::TextToPinyin(_T("你好"));
	EXPECT_TRUE(py.first.GetLength() > 0);
	EXPECT_TRUE(py.second.GetLength() > 0);
}

TEST(PinyinConvert, TextToPinyinMixed)
{
	auto py = CPinyinConvert::TextToPinyin(_T("hello世界"));
	EXPECT_TRUE(py.first.GetLength() > 0);
	EXPECT_TRUE(py.second.GetLength() > 0);
}

TEST(PinyinConvert, TextToPinyinEmpty)
{
	auto py = CPinyinConvert::TextToPinyin(_T(""));
	EXPECT_TRUE(py.first.IsEmpty());
	EXPECT_TRUE(py.second.IsEmpty());
}