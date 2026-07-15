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
    // 需要 Windows MFC 窗口环境（googletest 1.8 无 GTEST_SKIP，已 DISABLED_）
    SUCCEED();
}

TEST(ConvertRTFToText, DISABLED_PlainTextPassThrough)
{
    // 需要 Windows MFC 窗口环境（googletest 1.8 无 GTEST_SKIP，已 DISABLED_）
    SUCCEED();
}

TEST(ConvertRTFToText, DISABLED_EmptyString)
{
    // 需要 Windows MFC 窗口环境（googletest 1.8 无 GTEST_SKIP，已 DISABLED_）
    SUCCEED();
}
