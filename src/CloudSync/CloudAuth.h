#pragma once
#include <afx.h>
#include <memory>
#include <string>

namespace httplib { class Client; }

struct HttpResult {
    int status = 0;
    std::string body;
    bool success = false;
};

class IHttpClient {
public:
    virtual ~IHttpClient() = default;
    virtual HttpResult Post(const std::string& path, const std::string& body, const std::string& contentType) = 0;
    // Bearer-authenticated POST used by the token refresh flow. Clients that do
    // not implement it report failure, so the caller falls back to re-login.
    virtual HttpResult PostWithBearer(const std::string& /*path*/, const std::string& /*body*/,
                                      const std::string& /*contentType*/, const std::string& /*bearerToken*/)
    {
        return HttpResult{};
    }
    virtual bool IsValid() const = 0;
    virtual std::string GetBaseUrl() const = 0;
};

class HttplibClientAdapter : public IHttpClient {
public:
    HttplibClientAdapter(const std::string& url);
    HttpResult Post(const std::string& path, const std::string& body, const std::string& contentType) override;
    HttpResult PostWithBearer(const std::string& path, const std::string& body,
                              const std::string& contentType, const std::string& bearerToken) override;
    bool IsValid() const override;
    std::string GetBaseUrl() const override;
private:
    std::unique_ptr<httplib::Client> m_client;
    std::string m_url;
};

struct LoginResult
{
	BOOL success = FALSE;
	CString deviceToken;
	CString refreshToken;
	CString deviceId;
	CString error;
};

class CCloudAuth
{
public:
	static LoginResult Login(const CString& serverUrl,
	                         const CString& username,
	                         const CString& password);

	static LoginResult Register(const CString& serverUrl,
	                            const CString& username,
	                            const CString& email,
	                            const CString& password);

	static BOOL IsLoggedIn();
	static void Logout();

	// Exchanges the stored refresh token for a new access/refresh pair. Returns
	// FALSE when nothing is stored or the server rejects the pair, in which case
	// the caller must fall back to an interactive re-login.
	static BOOL TryRefreshToken();

	static void SetHttpClientForTest(std::shared_ptr<IHttpClient> mockClient);
	static void ResetHttpClientForTest();

private:
	static std::unique_ptr<httplib::Client> m_httpClient;
	static CString m_httpClientUrl;
	static std::shared_ptr<IHttpClient> m_testClient;

	static void EnsureHttpClient(const CString& serverUrl);
	static BOOL TryRefreshTokenLocked();
	static HttpResult DoPost(const std::string& path, const std::string& body, const std::string& contentType);
	static HttpResult DoPostWithBearer(const std::string& path, const std::string& body,
	                                   const std::string& contentType, const std::string& bearerToken);
};
