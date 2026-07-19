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
    virtual bool IsValid() const = 0;
    virtual std::string GetBaseUrl() const = 0;
};

class HttplibClientAdapter : public IHttpClient {
public:
    HttplibClientAdapter(const std::string& url);
    HttpResult Post(const std::string& path, const std::string& body, const std::string& contentType) override;
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

	static void SetHttpClientForTest(std::shared_ptr<IHttpClient> mockClient);
	static void ResetHttpClientForTest();

private:
	static std::unique_ptr<httplib::Client> m_httpClient;
	static CString m_httpClientUrl;
	static std::shared_ptr<IHttpClient> m_testClient;

	static void EnsureHttpClient(const CString& serverUrl);
	static HttpResult DoPost(const std::string& path, const std::string& body, const std::string& contentType);
};
