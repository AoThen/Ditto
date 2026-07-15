#include "stdafx.h"
#include "gtest/gtest.h"
#include "../src/ConvertRTFToText.h"

TEST(ConvertRTFToText, ConstructAndDestroy)
{
    CConvertRTFToText converter;
    SUCCEED();
}

TEST(ConvertRTFToText, DISABLED_SimpleRtfToText)
{
    GTEST_SKIP() << "需要 Windows MFC 窗口环境";
}

TEST(ConvertRTFToText, DISABLED_PlainTextPassThrough)
{
    GTEST_SKIP() << "需要 Windows MFC 窗口环境";
}

TEST(ConvertRTFToText, DISABLED_EmptyString)
{
    GTEST_SKIP() << "需要 Windows MFC 窗口环境";
}
