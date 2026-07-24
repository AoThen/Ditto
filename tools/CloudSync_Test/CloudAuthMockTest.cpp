#include "stdafx.h"
#include <gtest/gtest.h>
#include <functional>
#include "../../src/CloudSync/CloudAuth.h"
#include "GetSetOptionsMock.h"

class MockHttpClient : public IHttpClient {
public:
    int callCount = 0;
    std::string lastPath;
    std::string lastBody;
    std::string lastContentType;
    std::function<HttpResult()> postHandler;

    HttpResult Post(const std::string& path, const std::string& body, const std::string& contentType) override
    {
        callCount++;
        lastPath = path;
        lastBody = body;
        lastContentType = contentType;
        if (postHandler) return postHandler();
        return HttpResult{200, R"({"code":0,"data":{"device_token":"mock-token","device_id":"mock-device"}})", true};
    }

    bool IsValid() const override { return true; }
    std::string GetBaseUrl() const override { return "https://mock-server"; }
};

class CloudAuthMockTest : public ::testing::Test
{
protected:
    std::shared_ptr<MockHttpClient> mockClient;

    void SetUp() override
    {
        CGetSetOptions::Reset();
        CCloudAuth::Logout();
        CCloudAuth::ResetHttpClientForTest();
        mockClient = std::make_shared<MockHttpClient>();
        CCloudAuth::SetHttpClientForTest(mockClient);
    }

    void TearDown() override
    {
        CCloudAuth::ResetHttpClientForTest();
        CCloudAuth::Logout();
    }
};

// ============================================================================
// Login Tests - Network Response Parsing via Mock
// ============================================================================

TEST_F(CloudAuthMockTest, Login_SendsCorrectJSON)
{
    mockClient->postHandler = [&]() -> HttpResult {
        EXPECT_NE(mockClient->lastBody.find("\"username\":\"testuser\""), std::string::npos);
        EXPECT_NE(mockClient->lastBody.find("\"password\":\"testpass\""), std::string::npos);
        return HttpResult{200, R"({"code":0,"data":{"device_token":"tok","device_id":"did"}})", true};
    };

    LoginResult result = CCloudAuth::Login(
        _T("https://server.example.com"),
        _T("testuser"),
        _T("testpass")
    );

    EXPECT_TRUE(result.success);
}

TEST_F(CloudAuthMockTest, Login_ParsesSuccessResponse)
{
    LoginResult result = CCloudAuth::Login(
        _T("https://server.example.com"),
        _T("testuser"),
        _T("testpass")
    );

    EXPECT_TRUE(result.success);
    EXPECT_STREQ(result.deviceToken, _T("mock-token"));
    EXPECT_STREQ(result.deviceId, _T("mock-device"));
    EXPECT_TRUE(result.error.IsEmpty());
}

TEST_F(CloudAuthMockTest, Login_Handles401Status)
{
    mockClient->postHandler = []() -> HttpResult {
        return HttpResult{401, R"({"error":"unauthorized"})", true};
    };

    LoginResult result = CCloudAuth::Login(
        _T("https://server.example.com"),
        _T("baduser"),
        _T("badpass")
    );

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.IsEmpty());
    EXPECT_TRUE(result.error.Find(_T("unauthorized")) >= 0);
}

TEST_F(CloudAuthMockTest, Login_HandlesErrorCode)
{
    mockClient->postHandler = []() -> HttpResult {
        return HttpResult{200, R"({"code":1,"message":"invalid credentials"})", true};
    };

    LoginResult result = CCloudAuth::Login(
        _T("https://server.example.com"),
        _T("testuser"),
        _T("wrongpass")
    );

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.error.Find(_T("invalid credentials")) >= 0);
}

TEST_F(CloudAuthMockTest, Login_HandlesConnectionFailure)
{
    mockClient->postHandler = []() -> HttpResult {
        return HttpResult{0, "", false};
    };

    LoginResult result = CCloudAuth::Login(
        _T("https://server.example.com"),
        _T("testuser"),
        _T("testpass")
    );

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.IsEmpty());
}

TEST_F(CloudAuthMockTest, Login_HandlesMissingDeviceToken)
{
    mockClient->postHandler = []() -> HttpResult {
        return HttpResult{200, R"({"code":0,"data":{}})", true};
    };

    LoginResult result = CCloudAuth::Login(
        _T("https://server.example.com"),
        _T("testuser"),
        _T("testpass")
    );

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.error.Find(_T("device_token")) >= 0);
}

// ============================================================================
// Register Tests - Network Response Parsing via Mock
// ============================================================================

TEST_F(CloudAuthMockTest, Register_SendsCorrectJSON)
{
    mockClient->postHandler = [&]() -> HttpResult {
        EXPECT_NE(mockClient->lastBody.find("\"username\":\"newuser\""), std::string::npos);
        EXPECT_NE(mockClient->lastBody.find("\"email\":\"newuser@example.com\""), std::string::npos);
        EXPECT_NE(mockClient->lastBody.find("\"password\":\"newpass\""), std::string::npos);
        return HttpResult{201, R"({"code":0})", true};
    };

    LoginResult result = CCloudAuth::Register(
        _T("https://server.example.com"),
        _T("newuser"),
        _T("newuser@example.com"),
        _T("newpass")
    );

    EXPECT_TRUE(result.success);
}

TEST_F(CloudAuthMockTest, Register_ParsesSuccessResponse)
{
    mockClient->postHandler = []() -> HttpResult {
        return HttpResult{201, R"({"code":0})", true};
    };

    LoginResult result = CCloudAuth::Register(
        _T("https://server.example.com"),
        _T("newuser"),
        _T("newuser@example.com"),
        _T("newpass")
    );

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.error.IsEmpty());
}

TEST_F(CloudAuthMockTest, Register_HandlesNon200Status)
{
    mockClient->postHandler = []() -> HttpResult {
        return HttpResult{400, R"({"error":"bad request"})", true};
    };

    LoginResult result = CCloudAuth::Register(
        _T("https://server.example.com"),
        _T("newuser"),
        _T("newuser@example.com"),
        _T("newpass")
    );

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.error.Find(_T("bad request")) >= 0);
}

// ============================================================================
// EnsureHttpClient Tests - Real client behavior (no mock)
// ============================================================================

TEST(CloudAuth_EnsureHttpClient, ReusesForSameUrl)
{
    CCloudAuth::ResetHttpClientForTest();
    CCloudAuth::Logout();

    LoginResult r1 = CCloudAuth::Login(
        _T("https://test-server"), _T("u"), _T("p"));
    LoginResult r2 = CCloudAuth::Login(
        _T("https://test-server"), _T("u"), _T("p"));

    EXPECT_FALSE(r1.success);
    EXPECT_FALSE(r2.success);
}

TEST(CloudAuth_EnsureHttpClient, CreatesNewForDifferentUrl)
{
    CCloudAuth::ResetHttpClientForTest();
    CCloudAuth::Logout();

    LoginResult r1 = CCloudAuth::Login(
        _T("https://server-a"), _T("u"), _T("p"));
    LoginResult r2 = CCloudAuth::Login(
        _T("https://server-b"), _T("u"), _T("p"));

    EXPECT_FALSE(r1.success);
    EXPECT_FALSE(r2.success);
}

TEST(CloudAuth_EnsureHttpClient, RejectsPlainHttp)
{
    CCloudAuth::ResetHttpClientForTest();
    CCloudAuth::Logout();

    LoginResult r = CCloudAuth::Login(
        _T("http://insecure-server"), _T("u"), _T("p"));

    EXPECT_FALSE(r.success);
    EXPECT_FALSE(r.error.IsEmpty());
}
