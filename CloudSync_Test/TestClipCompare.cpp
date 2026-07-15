#include <gtest/gtest.h>
#include "../src/ClipCompare.h"

TEST(ClipCompare, ConstructAndDestroy) {
	CClipCompare compare;
	SUCCEED();
}

TEST(ClipCompare, DISABLED_CompareIdenticalClips) {
	GTEST_SKIP() << "需要 mock CClip 和数据库接口";
}

TEST(ClipCompare, DISABLED_CompareDifferentClips) {
	GTEST_SKIP() << "需要 mock CClip 和数据库接口";
}
