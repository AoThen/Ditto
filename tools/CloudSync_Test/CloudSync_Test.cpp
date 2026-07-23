// CloudSync_Test.cpp - Google Test entry point for CloudSync module
// Tests: CloudCrypto (base64, AES-256-GCM, PBKDF2), JSON serialization, HDROP filtering

#include "stdafx.h"
#include <gtest/gtest.h>

// Google Test main
int main(int argc, char** argv)
{
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
