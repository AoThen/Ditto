#include <gtest/gtest.h>
#include <string>
#include "../Addins/DittoUtil/RemoveLineFeeds.h"
#include "IClipMock.h"

TEST(RemoveLineFeeds, ConstructAndDestroy)
{
    CRemoveLineFeeds rlf;
    SUCCEED();
}

TEST(RemoveLineFeeds, FullIntegrationTest)
{
    CMockClip clip;
    CDittoInfo dittoInfo;

    clip.Formats().AddTextFormat(CF_TEXT, "Hello\r\nWorld\r\n");

    CRemoveLineFeeds rlf;
    bool result = rlf.RemoveLineFeeds(dittoInfo, &clip);

    EXPECT_TRUE(result);

    IClipFormats* formats = clip.Clips();
    ASSERT_NE(formats, nullptr);
    IClipFormat* fmt = formats->FindFormatEx(CF_TEXT);
    ASSERT_NE(fmt, nullptr);

    HGLOBAL hData = fmt->Data();
    ASSERT_NE(hData, nullptr);
    char* text = (char*)GlobalLock(hData);
    ASSERT_NE(text, nullptr);

    std::string actual(text);
    GlobalUnlock(hData);

    EXPECT_EQ(actual.find("\r\n"), std::string::npos);
    EXPECT_NE(actual.find("Hello World"), std::string::npos);
}

TEST(RemoveLineFeeds, NoLineFeedsLeavesDataUnchanged)
{
    CMockClip clip;
    CDittoInfo dittoInfo;

    clip.Formats().AddTextFormat(CF_TEXT, "SingleLine");

    CRemoveLineFeeds rlf;
    bool result = rlf.RemoveLineFeeds(dittoInfo, &clip);

    EXPECT_TRUE(result);

    IClipFormats* formats = clip.Clips();
    ASSERT_NE(formats, nullptr);
    IClipFormat* fmt = formats->FindFormatEx(CF_TEXT);
    ASSERT_NE(fmt, nullptr);

    HGLOBAL hData = fmt->Data();
    ASSERT_NE(hData, nullptr);
    char* text = (char*)GlobalLock(hData);
    ASSERT_NE(text, nullptr);

    std::string actual(text);
    GlobalUnlock(hData);

    EXPECT_EQ(actual, "SingleLine");
    EXPECT_EQ(actual.find("\r\n"), std::string::npos);
}

TEST(RemoveLineFeeds, HandlesMultipleLineFeeds)
{
    CMockClip clip;
    CDittoInfo dittoInfo;

    clip.Formats().AddTextFormat(CF_TEXT, "Line1\r\nLine2\r\nLine3");

    CRemoveLineFeeds rlf;
    bool result = rlf.RemoveLineFeeds(dittoInfo, &clip);

    EXPECT_TRUE(result);

    IClipFormat* fmt = clip.Clips()->FindFormatEx(CF_TEXT);
    ASSERT_NE(fmt, nullptr);

    char* text = (char*)GlobalLock(fmt->Data());
    ASSERT_NE(text, nullptr);

    std::string actual(text);
    GlobalUnlock(fmt->Data());

    EXPECT_EQ(actual.find("\r\n"), std::string::npos);
    EXPECT_EQ(actual, "Line1 Line2 Line3");
}

TEST(RemoveLineFeeds, EmptyTextFormat)
{
    CMockClip clip;
    CDittoInfo dittoInfo;

    clip.Formats().AddTextFormat(CF_TEXT, "");

    CRemoveLineFeeds rlf;
    bool result = rlf.RemoveLineFeeds(dittoInfo, &clip);

    EXPECT_TRUE(result);

    IClipFormat* fmt = clip.Clips()->FindFormatEx(CF_TEXT);
    ASSERT_NE(fmt, nullptr);

    char* text = (char*)GlobalLock(fmt->Data());
    ASSERT_NE(text, nullptr);

    std::string actual(text);
    GlobalUnlock(fmt->Data());

    EXPECT_EQ(actual, "");
}

TEST(RemoveLineFeeds, NoTextFormatReturnsFalse)
{
    CMockClip clip;
    CDittoInfo dittoInfo;

    clip.Formats().AddTextFormat(RegisterClipboardFormat(_T("CustomFormat")), "data");

    CRemoveLineFeeds rlf;
    bool result = rlf.RemoveLineFeeds(dittoInfo, &clip);

    EXPECT_FALSE(result);
}
