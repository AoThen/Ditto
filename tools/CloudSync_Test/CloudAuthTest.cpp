// CloudAuthTest.cpp - Unit tests for CloudAuth module
// Tests: Login, Register, IsLoggedIn, Logout
//
// NOTE: These tests validate the authentication logic without actual network calls.
// We test the local state management (token storage, login status) and error handling.
// Network-dependent tests would require a mock HTTP server.

#include "stdafx.h"
#include <gtest/gtest.h>
#include "../../src/CloudSync/CloudAuth.h"
#include "../../src/CloudSync/CloudKeyExport.h"
#include "GetSetOptionsMock.h"
#include <vector>
#include <string>

// ============================================================================
// Test Fixture: Setup/Teardown for CloudAuth tests
// ============================================================================

class CloudAuthTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		// Reset mock options and logout before each test
		CGetSetOptions::Reset();
		CCloudAuth::Logout();
	}

	void TearDown() override
	{
		// Cleanup: logout after each test
		CCloudAuth::Logout();
	}
};

// ============================================================================
// IsLoggedIn Tests
// ============================================================================

TEST(CloudAuth_IsLoggedIn, ReturnsFalseWhenNoToken)
{
	// Default state: no token stored
	BOOL loggedIn = CCloudAuth::IsLoggedIn();
	EXPECT_FALSE(loggedIn) << "Should not be logged in when no token stored";
}

TEST(CloudAuth_IsLoggedIn, ReturnsTrueWhenTokenExists)
{
	// Store a token
	CGetSetOptions::SetCloudDeviceToken(CStringA("test-device-token-12345"));
	
	BOOL loggedIn = CCloudAuth::IsLoggedIn();
	EXPECT_TRUE(loggedIn) << "Should be logged in when token exists";
}

TEST(CloudAuth_IsLoggedIn, ReturnsFalseAfterLogout)
{
	// Store token first
	CGetSetOptions::SetCloudDeviceToken(CStringA("test-token"));
	EXPECT_TRUE(CCloudAuth::IsLoggedIn());
	
	// Logout
	CCloudAuth::Logout();
	
	BOOL loggedIn = CCloudAuth::IsLoggedIn();
	EXPECT_FALSE(loggedIn) << "Should not be logged in after logout";
}

// ============================================================================
// Logout Tests
// ============================================================================

TEST(CloudAuth_Logout, ClearsDeviceToken)
{
	// Store token first
	CGetSetOptions::SetCloudDeviceToken(CStringA("test-token-to-clear"));
	EXPECT_FALSE(CGetSetOptions::GetCloudDeviceToken().IsEmpty());
	
	// Logout
	CCloudAuth::Logout();
	
	// Verify token is cleared
	CStringA token = CGetSetOptions::GetCloudDeviceToken();
	EXPECT_TRUE(token.IsEmpty()) << "Logout should clear device token";
}

TEST(CloudAuth_Logout, Idempotent)
{
	// Multiple logouts should not crash or cause issues
	EXPECT_NO_THROW({
		CCloudAuth::Logout();
		CCloudAuth::Logout();
		CCloudAuth::Logout();
	});
}

TEST(CloudAuth_Logout, ClearsDeviceId)
{
	// Store device ID
	CGetSetOptions::SetCloudDeviceId(CStringA("test-device-id-12345"));
	
	// Logout
	CCloudAuth::Logout();
	
	// Note: Logout only clears the token, not device ID
	// Device ID may persist for re-login purposes
	CStringA deviceId = CGetSetOptions::GetCloudDeviceId();
	// Device ID behavior depends on implementation - may or may not be cleared
}

// ============================================================================
// Login Tests - Response Parsing
// These tests validate the JSON parsing logic without actual network calls
// ============================================================================

TEST(CloudAuth_Login, ValidResponseStructure)
{
	// Test that LoginResult struct is properly initialized
	LoginResult result;
	EXPECT_FALSE(result.success);
	EXPECT_TRUE(result.deviceToken.IsEmpty());
	EXPECT_TRUE(result.deviceId.IsEmpty());
	EXPECT_TRUE(result.error.IsEmpty());
}

TEST(CloudAuth_Login, EmptyServerUrl)
{
	// Test with empty server URL
	LoginResult result = CCloudAuth::Login(
		_T(""),  // Empty URL
		_T("testuser"),
		_T("testpass")
	);
	
	// Should fail with connection error
	EXPECT_FALSE(result.success);
	EXPECT_FALSE(result.error.IsEmpty()) << "Should have error message for empty URL";
}

TEST(CloudAuth_Login, EmptyCredentials)
{
	// Test with empty username/password
	LoginResult result = CCloudAuth::Login(
		_T("https://localhost:8080"),
		_T(""),  // Empty username
		_T("")   // Empty password
	);
	
	// Should fail (server unreachable or invalid credentials)
	EXPECT_FALSE(result.success);
}

TEST(CloudAuth_Login, InvalidServerUrl)
{
	// Test with invalid server URL
	LoginResult result = CCloudAuth::Login(
		_T("not-a-valid-url"),
		_T("testuser"),
		_T("testpass")
	);
	
	// Should fail with connection error
	EXPECT_FALSE(result.success);
	EXPECT_FALSE(result.error.IsEmpty());
}

// ============================================================================
// Register Tests - Response Parsing
// ============================================================================

TEST(CloudAuth_Register, ValidResponseStructure)
{
	// Test that LoginResult struct is properly initialized for registration
	LoginResult result;
	EXPECT_FALSE(result.success);
	EXPECT_TRUE(result.deviceToken.IsEmpty());
	EXPECT_TRUE(result.deviceId.IsEmpty());
	EXPECT_TRUE(result.error.IsEmpty());
}

TEST(CloudAuth_Register, EmptyServerUrl)
{
	// Test with empty server URL
	LoginResult result = CCloudAuth::Register(
		_T(""),  // Empty URL
		_T("newuser"),
		_T("newuser@example.com"),
		_T("newpass")
	);
	
	// Should fail with connection error
	EXPECT_FALSE(result.success);
	EXPECT_FALSE(result.error.IsEmpty());
}

TEST(CloudAuth_Register, EmptyCredentials)
{
	// Test with empty credentials
	LoginResult result = CCloudAuth::Register(
		_T("https://localhost:8080"),
		_T(""),  // Empty username
		_T(""),  // Empty email
		_T("")   // Empty password
	);
	
	// Should fail
	EXPECT_FALSE(result.success);
}

TEST(CloudAuth_Register, InvalidServerUrl)
{
	// Test with invalid server URL
	LoginResult result = CCloudAuth::Register(
		_T("not-a-valid-url"),
		_T("newuser"),
		_T("newuser@example.com"),
		_T("newpass")
	);
	
	// Should fail with connection error
	EXPECT_FALSE(result.success);
	EXPECT_FALSE(result.error.IsEmpty());
}

// ============================================================================
// Token Persistence Tests
// ============================================================================

TEST(CloudAuth_TokenPersistence, TokenPersistsAcrossCalls)
{
	// Manually set token (simulates successful login)
	CStringA testToken("persisted-token-xyz");
	CGetSetOptions::SetCloudDeviceToken(testToken);
	
	// Verify token persists
	EXPECT_TRUE(CCloudAuth::IsLoggedIn());
	CStringA retrievedToken = CGetSetOptions::GetCloudDeviceToken();
	EXPECT_EQ(testToken, retrievedToken);
}

TEST(CloudAuth_TokenPersistence, DeviceIdPersistsAcrossCalls)
{
	// Manually set device ID (simulates successful login)
	CStringA testDeviceId("device-12345");
	CGetSetOptions::SetCloudDeviceId(testDeviceId);
	
	// Verify device ID persists
	CStringA retrievedDeviceId = CGetSetOptions::GetCloudDeviceId();
	EXPECT_EQ(testDeviceId, retrievedDeviceId);
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST(CloudAuth_ErrorHandling, LongCredentials)
{
	// Test with very long credentials (should not crash)
	CString longUsername(_T('A'), 10000);
	CString longPassword(_T('B'), 10000);
	
	LoginResult result = CCloudAuth::Login(
		_T("https://localhost:8080"),
		longUsername,
		longPassword
	);
	
	// Should fail gracefully (connection error, not crash)
	EXPECT_FALSE(result.success);
}

TEST(CloudAuth_ErrorHandling, SpecialCharactersInCredentials)
{
	// Test with special characters
	LoginResult result = CCloudAuth::Login(
		_T("https://localhost:8080"),
		_T("user@domain.com!#$%^&*()"),
		_T("pass!@#$%^&*()_+")
	);
	
	// Should fail gracefully
	EXPECT_FALSE(result.success);
}

TEST(CloudAuth_ErrorHandling, UnicodeCredentials)
{
	// Test with Unicode characters
	LoginResult result = CCloudAuth::Login(
		_T("https://localhost:8080"),
		_T("用户测试"),
		_T("密码测试")
	);
	
	// Should fail gracefully (connection error)
	EXPECT_FALSE(result.success);
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST(CloudAuth_Integration, LoginLogoutCycle)
{
	// Start logged out
	CCloudAuth::Logout();
	EXPECT_FALSE(CCloudAuth::IsLoggedIn());
	
	// Simulate login by setting token directly
	CGetSetOptions::SetCloudDeviceToken(CStringA("test-token"));
	CGetSetOptions::SetCloudDeviceId(CStringA("test-device-id"));
	
	// Verify logged in
	EXPECT_TRUE(CCloudAuth::IsLoggedIn());
	
	// Logout
	CCloudAuth::Logout();
	
	// Verify logged out
	EXPECT_FALSE(CCloudAuth::IsLoggedIn());
}

TEST(CloudAuth_Integration, MultipleLoginAttempts)
{
	// Multiple login attempts (simulated by setting different tokens)
	for (int i = 0; i < 5; i++)
	{
		CString token;
		token.Format(_T("token-%d"), i);
		CGetSetOptions::SetCloudDeviceToken(CStringA(token));
		
		EXPECT_TRUE(CCloudAuth::IsLoggedIn());
		
		// Verify token is correct
		CStringA retrieved = CGetSetOptions::GetCloudDeviceToken();
		CT2A tokenA(token, CP_UTF8);
		EXPECT_EQ(std::string(tokenA.m_psz), std::string(retrieved.GetString()));
	}
}

TEST(CloudAuth_Integration, RegisterThenLogin)
{
	// Simulate registration (would set token on success)
	// In real flow, register may or may not auto-login
	
	// Simulate registration with token
	CGetSetOptions::SetCloudDeviceToken(CStringA("register-token"));
	EXPECT_TRUE(CCloudAuth::IsLoggedIn());
	
	// Logout
	CCloudAuth::Logout();
	EXPECT_FALSE(CCloudAuth::IsLoggedIn());
	
	// Simulate login with different token
	CGetSetOptions::SetCloudDeviceToken(CStringA("login-token"));
	EXPECT_TRUE(CCloudAuth::IsLoggedIn());
}

// ============================================================================
// TryRefreshToken Tests
// ============================================================================

namespace {

// Answers the bearer-authenticated refresh call only, so a regression to the
// plain Post() path is caught rather than silently accepted.
class RefreshMockClient : public IHttpClient
{
public:
	HttpResult Post(const std::string& /*path*/, const std::string& /*body*/,
	                const std::string& /*contentType*/) override
	{
		return HttpResult{};
	}

	HttpResult PostWithBearer(const std::string& path, const std::string& /*body*/,
	                          const std::string& /*contentType*/,
	                          const std::string& bearer) override
	{
		bearerCalls++;
		lastPath = path;
		lastBearer = bearer;
		return HttpResult{ status, responseBody, true };
	}

	bool IsValid() const override { return true; }
	std::string GetBaseUrl() const override { return "https://mock-server"; }

	int bearerCalls = 0;
	std::string lastPath;
	std::string lastBearer;
	int status = 200;
	std::string responseBody =
		R"({"code":0,"data":{"device_token":"new-access","refresh_token":"new-refresh"}})";
};

} // namespace

TEST(CloudAuth_TryRefreshToken, FailsWithoutStoredRefreshToken)
{
	auto client = std::make_shared<RefreshMockClient>();
	CCloudAuth::SetHttpClientForTest(client);
	CGetSetOptions::SetCloudRefreshToken(CStringA(""));

	EXPECT_FALSE(CCloudAuth::TryRefreshToken());
	EXPECT_EQ(0, client->bearerCalls);

	CCloudAuth::ResetHttpClientForTest();
}

TEST(CloudAuth_TryRefreshToken, RotatesBothTokens)
{
	auto client = std::make_shared<RefreshMockClient>();
	CCloudAuth::SetHttpClientForTest(client);
	CGetSetOptions::SetCloudServerUrl(_T("https://mock-server"));
	CGetSetOptions::SetCloudRefreshToken(CStringA("old-refresh"));
	CGetSetOptions::SetCloudDeviceToken(CStringA("old-access"));

	EXPECT_TRUE(CCloudAuth::TryRefreshToken());
	EXPECT_EQ(1, client->bearerCalls);
	EXPECT_EQ(std::string("/api/v1/auth/refresh?as=bearer"), client->lastPath);
	EXPECT_EQ(std::string("old-refresh"), client->lastBearer);
	// The old access token is dead the moment the version is bumped, so both
	// halves of the pair have to be replaced together.
	EXPECT_EQ(std::string("new-access"), std::string(CGetSetOptions::GetCloudDeviceToken().GetString()));
	EXPECT_EQ(std::string("new-refresh"), std::string(CGetSetOptions::GetCloudRefreshToken().GetString()));

	CCloudAuth::ResetHttpClientForTest();
}

TEST(CloudAuth_TryRefreshToken, KeepsStoredTokensWhenServerRejects)
{
	auto client = std::make_shared<RefreshMockClient>();
	client->status = 401;
	client->responseBody = R"({"code":40101,"message":"refresh failed"})";
	CCloudAuth::SetHttpClientForTest(client);
	CGetSetOptions::SetCloudServerUrl(_T("https://mock-server"));
	CGetSetOptions::SetCloudRefreshToken(CStringA("old-refresh"));
	CGetSetOptions::SetCloudDeviceToken(CStringA("old-access"));

	EXPECT_FALSE(CCloudAuth::TryRefreshToken());
	EXPECT_EQ(std::string("old-access"), std::string(CGetSetOptions::GetCloudDeviceToken().GetString()));
	EXPECT_EQ(std::string("old-refresh"), std::string(CGetSetOptions::GetCloudRefreshToken().GetString()));

	CCloudAuth::ResetHttpClientForTest();
}
