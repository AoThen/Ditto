#include <gtest/gtest.h>
#include "../src/ClipCompare.h"

TEST(ClipCompare, ConstructAndDestroy) {
	CClipCompare compare;
	SUCCEED();
}

TEST(ClipCompare, DISABLED_CompareIdenticalClips) {
	// 需要 mock CClip 和数据库接口（googletest 1.8 无 GTEST_SKIP，已 DISABLED_）
	SUCCEED();
}

TEST(ClipCompare, DISABLED_CompareDifferentClips) {
	// 需要 mock CClip 和数据库接口（googletest 1.8 无 GTEST_SKIP，已 DISABLED_）
	SUCCEED();
}
