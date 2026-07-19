#include "stdafx.h"
#include "gtest/gtest.h"
#include "../src/ConvertRTFToText.h"
#include <string>
#include <cstdlib>
#include <cctype>

static std::string StripRtfTags(const std::string& rtf)
{
    std::string result;
    size_t i = 0;
    while (i < rtf.size())
    {
        if (rtf[i] == '\\')
        {
            ++i;
            if (i < rtf.size())
            {
                if (rtf[i] == '\'')
                {
                    ++i;
                    if (i + 1 < rtf.size())
                    {
                        char hex[3] = { rtf[i], rtf[i + 1], 0 };
                        char* end = nullptr;
                        int code = (int)strtol(hex, &end, 16);
                        if (end == hex + 2)
                            result += static_cast<char>(code);
                        i += 2;
                    }
                }
                else
                {
                    while (i < rtf.size() && (isalpha(static_cast<unsigned char>(rtf[i])) || rtf[i] == '*'))
                        ++i;
                    if (i < rtf.size() && rtf[i] == ' ')
                        ++i;
                }
            }
        }
        else if (rtf[i] == '{' || rtf[i] == '}')
        {
            ++i;
        }
        else if (rtf[i] == '\r')
        {
            ++i;
            if (i < rtf.size() && rtf[i] == '\n') ++i;
        }
        else if (rtf[i] == '\n')
        {
            ++i;
        }
        else
        {
            result += rtf[i];
            ++i;
        }
    }
    size_t start = result.find_first_not_of(" \t\r\n");
    size_t end = result.find_last_not_of(" \t\r\n");
    if (start == std::string::npos)
        return std::string();
    return result.substr(start, end - start + 1);
}

TEST(ConvertRTFToText, ConstructAndDestroy)
{
    CConvertRTFToText converter;
    SUCCEED();
}

TEST(ConvertRTFToText, StripRtfTags_SimpleText)
{
    EXPECT_EQ(StripRtfTags("{\\rtf1 Hello}"), "Hello");
}

TEST(ConvertRTFToText, StripRtfTags_WithFormatting)
{
    EXPECT_EQ(StripRtfTags("{\\rtf1\\b Bold}"), "Bold");
}

TEST(ConvertRTFToText, StripRtfTags_EmptyInput)
{
    EXPECT_EQ(StripRtfTags(""), "");
}

TEST(ConvertRTFToText, StripRtfTags_PlainTextOnly)
{
    EXPECT_EQ(StripRtfTags("Hello World"), "Hello World");
}

TEST(ConvertRTFToText, StripRtfTags_UnicodeEscape)
{
    std::string expected = "\xE9";
    EXPECT_EQ(StripRtfTags("\\'e9"), expected);
}

TEST(ConvertRTFToText, StripRtfTags_MultipleTags)
{
    EXPECT_EQ(StripRtfTags("{\\rtf1\\i\\b Mixed\\i0 Normal}"), "Mixed Normal");
}

TEST(ConvertRTFToText, StripRtfTags_NestedBraces)
{
    EXPECT_EQ(StripRtfTags("{\\rtf1 Outer {\\b Inner}}"), "Outer Inner");
}
