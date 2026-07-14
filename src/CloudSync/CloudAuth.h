#pragma once
#include <afx.h>
#include <memory>

namespace httplib { class Client; }

struct LoginResult
{
	BOOL success;
	CString deviceToken;
	CString deviceId;
	CString error;
};

class CCloudAuth
{
public:
	// Login to cloud server
	static LoginResult Login(const CString& serverUrl,
	                         const CString& username,
	                         const CString& password);

	// Register new account
	static LoginResult Register(const CString& serverUrl,
	                            const CString& username,
	                            const CString& email,
	                            const CString& password);

	// Check if currently logged in (token exists)
	static BOOL IsLoggedIn();

	// Logout (clear local token)
	static void Logout();

private:
	// Reusable HTTP client for auth API calls
	static std::unique_ptr<httplib::Client> m_httpClient;
	static CString m_httpClientUrl;

	// Create or reuse HTTP client for the given server URL
	static void EnsureHttpClient(const CString& serverUrl);
};
