#include "stdafx.h"
#include "CloudAuth.h"
#include "../httplib.h"
#include "../json.hpp"
#include "../Options.h"
#include "../Misc.h"

#include <mutex>

using json = nlohmann::json;

// Static member definitions
std::unique_ptr<httplib::Client> CCloudAuth::m_httpClient;
CString CCloudAuth::m_httpClientUrl;
std::shared_ptr<IHttpClient> CCloudAuth::m_testClient;

// Helper: convert CString to std::string (with null safety)
static std::string CStringToStdString(const CString& str)
{
	if (str.IsEmpty())
		return std::string();
	CT2A utf8(str, CP_UTF8);
	if (utf8.m_psz == nullptr)
		return std::string();
	return std::string(utf8.m_psz);
}

void CCloudAuth::EnsureHttpClient(const CString& serverUrl)
{
	if (m_testClient) return;

	std::string url = CStringToStdString(serverUrl);
	// Enforce HTTPS: reject plain http for security
	if (url.find("https://") != 0)
	{
		if (url.find("http://") == 0)
		{
			OutputDebugStringA("[CloudAuth] ERROR: HTTPS required, refusing to use plain HTTP.\n");
			m_httpClient.reset();
			m_httpClientUrl.Empty();
			return;
		}
		// No scheme - default to https
		url = "https://" + url;
	}
	if (!m_httpClient || m_httpClientUrl != serverUrl)
	{
		m_httpClient = std::make_unique<httplib::Client>(url);
		m_httpClient->set_connection_timeout(10, 0);
		m_httpClient->set_read_timeout(30, 0);
		m_httpClient->set_write_timeout(30, 0);
		m_httpClientUrl = serverUrl;
	}
}

// ============================================================================
// HttplibClientAdapter - Real HTTP client wrapping httplib::Client
// ============================================================================

HttplibClientAdapter::HttplibClientAdapter(const std::string& url)
	: m_url(url)
{
	m_client = std::make_unique<httplib::Client>(url);
	m_client->set_connection_timeout(10, 0);
	m_client->set_read_timeout(30, 0);
	m_client->set_write_timeout(30, 0);
}

HttpResult HttplibClientAdapter::Post(const std::string& path, const std::string& body, const std::string& contentType)
{
	if (!m_client) return HttpResult{};
	auto res = m_client->Post(path, body, contentType);
	if (!res) return HttpResult{};
	return HttpResult{res->status, res->body, true};
}

HttpResult HttplibClientAdapter::PostWithBearer(const std::string& path, const std::string& body,
                                                const std::string& contentType, const std::string& bearerToken)
{
	if (!m_client) return HttpResult{};
	httplib::Headers headers = {
		{ "Authorization", "Bearer " + bearerToken }
	};
	auto res = m_client->Post(path, headers, body, contentType);
	if (!res) return HttpResult{};
	return HttpResult{res->status, res->body, true};
}

bool HttplibClientAdapter::IsValid() const
{
	return m_client != nullptr;
}

std::string HttplibClientAdapter::GetBaseUrl() const
{
	return m_url;
}

// ============================================================================
// DoPost - Dispatch HTTP POST to either test client or real client
// ============================================================================

HttpResult CCloudAuth::DoPost(const std::string& path, const std::string& body, const std::string& contentType)
{
	if (m_testClient)
	{
		return m_testClient->Post(path, body, contentType);
	}
	if (!m_httpClient)
	{
		return HttpResult{};
	}
	auto res = m_httpClient->Post(path, body, contentType);
	if (!res)
	{
		return HttpResult{};
	}
	return HttpResult{res->status, res->body, true};
}

// DoPostWithBearer - Same as DoPost, but authenticates with an explicit bearer
// token. Used by the refresh flow, whose token is not the stored access token.
HttpResult CCloudAuth::DoPostWithBearer(const std::string& path, const std::string& body,
                                        const std::string& contentType, const std::string& bearerToken)
{
	if (m_testClient)
	{
		return m_testClient->PostWithBearer(path, body, contentType, bearerToken);
	}
	if (!m_httpClient)
	{
		return HttpResult{};
	}
	httplib::Headers headers = {
		{ "Authorization", "Bearer " + bearerToken }
	};
	auto res = m_httpClient->Post(path, headers, body, contentType);
	if (!res)
	{
		return HttpResult{};
	}
	return HttpResult{res->status, res->body, true};
}

// Helper: convert std::string to CString (UTF-8 JSON → Unicode)
static CString StdStringToCString(const std::string& str)
{
	if (str.empty())
		return CString();
	return CString(CA2W(str.c_str(), CP_UTF8));
}

// Helper: stable per-install identifier sent on login. Without it the server
// keys devices by name, so every Ditto install of one account shares a single
// device row (and one token version).
static CString GetOrCreateInstallId()
{
	CString id = CGetSetOptions::GetCloudInstallId();
	if (id.IsEmpty())
	{
		id = NewGuidString();
		CGetSetOptions::SetCloudInstallId(id);
	}
	return id;
}

LoginResult CCloudAuth::Login(const CString& serverUrl,
                              const CString& username,
                              const CString& password)
{
	LoginResult result = {};
	result.success = FALSE;

	try
	{
		EnsureHttpClient(serverUrl);
		if (!m_testClient && !m_httpClient)
		{
			result.error = _T("Failed to create HTTP client");
			return result;
		}

		json body;
		body["username"] = CStringToStdString(username);
		body["password"] = CStringToStdString(password);
		body["device_id"] = CStringToStdString(GetOrCreateInstallId());

		std::string bodyStr = body.dump();
		HttpResult httpRes = DoPost("/api/v1/auth/login", bodyStr, "application/json");
		if (!httpRes.success)
		{
			result.error = _T("Failed to connect to server (network error)");
			return result;
		}

		if (httpRes.status != 200)
		{
			try
			{
				auto errJson = json::parse(httpRes.body);
				if (errJson.contains("error"))
				{
					result.error = StdStringToCString(errJson["error"].get<std::string>());
				}
				else if (errJson.contains("message"))
				{
					result.error = StdStringToCString(errJson["message"].get<std::string>());
				}
				else
				{
					result.error.Format(_T("Server returned HTTP %d"), httpRes.status);
				}
			}
			catch (const json::parse_error& e)
			{
				CString msg;
				msg.Format(_T("HTTP %d, parse error: %hs"), httpRes.status, e.what());
				result.error = msg;
			}
			catch (...)
			{
				result.error.Format(_T("Server returned HTTP %d"), httpRes.status);
			}
			return result;
		}

		// Parse response: { "code": 0, "data": { "device_token": "...", "device_id": "..." } }
		try
		{
			auto responseJson = json::parse(httpRes.body);

			// Check for error code
			if (responseJson.contains("code") && responseJson["code"].get<int>() != 0)
			{
				CString errorMsg;
				if (responseJson.contains("message"))
				{
					errorMsg = StdStringToCString(responseJson["message"].get<std::string>());
				}
				else
				{
					errorMsg.Format(_T("Server returned error code %d"), responseJson["code"].get<int>());
				}
				result.error = errorMsg;
				return result;
			}

			if (responseJson.contains("data") && responseJson["data"].contains("device_token"))
			{
				const auto& data = responseJson["data"];
				result.deviceToken = StdStringToCString(data["device_token"].get<std::string>());
				if (data.contains("device_id"))
				{
					result.deviceId = StdStringToCString(data["device_id"].get<std::string>());
					// Persist device_id
					CT2A deviceIdA(result.deviceId, CP_UTF8);
					CGetSetOptions::SetCloudDeviceId(deviceIdA);
				}
				result.success = TRUE;

				// Persist token via CGetSetOptions
				CT2A tokenA(result.deviceToken, CP_UTF8);
				CGetSetOptions::SetCloudDeviceToken(tokenA);

				// The access token is short-lived; the refresh token is what keeps
				// a long-running session alive across restarts.
				if (data.contains("refresh_token"))
				{
					result.refreshToken = StdStringToCString(data["refresh_token"].get<std::string>());
					CT2A refreshA(result.refreshToken, CP_UTF8);
					CGetSetOptions::SetCloudRefreshToken(refreshA);
				}
			}
			else
			{
				result.error = _T("Invalid server response: missing device_token");
			}
		}
		catch (const json::parse_error& e)
		{
			result.error.Format(_T("JSON parse error: %hs"), e.what());
		}
		catch (const std::exception& e)
		{
			result.error.Format(_T("Error: %hs"), e.what());
		}
	}
	catch (const std::exception& e)
	{
		result.error.Format(_T("Exception: %hs"), e.what());
	}

	return result;
}

LoginResult CCloudAuth::Register(const CString& serverUrl,
                                 const CString& username,
                                 const CString& email,
                                 const CString& password)
{
	LoginResult result = {};
	result.success = FALSE;

	try
	{
		EnsureHttpClient(serverUrl);
		if (!m_testClient && !m_httpClient)
		{
			result.error = _T("Failed to create HTTP client");
			return result;
		}

		json body;
		body["username"] = CStringToStdString(username);
		body["email"] = CStringToStdString(email);
		body["password"] = CStringToStdString(password);

		std::string bodyStr = body.dump();
		HttpResult httpRes = DoPost("/api/v1/auth/register", bodyStr, "application/json");
		if (!httpRes.success)
		{
			result.error = _T("Failed to connect to server (network error)");
			return result;
		}

		if (httpRes.status != 200 && httpRes.status != 201)
		{
			try
			{
				auto errJson = json::parse(httpRes.body);
				if (errJson.contains("error"))
				{
					result.error = StdStringToCString(errJson["error"].get<std::string>());
				}
				else if (errJson.contains("message"))
				{
					result.error = StdStringToCString(errJson["message"].get<std::string>());
				}
				else
				{
					result.error.Format(_T("Server returned HTTP %d"), httpRes.status);
				}
			}
			catch (const json::parse_error& e)
			{
				CString msg;
				msg.Format(_T("HTTP %d, parse error: %hs"), httpRes.status, e.what());
				result.error = msg;
			}
			catch (...)
			{
				result.error.Format(_T("Server returned HTTP %d"), httpRes.status);
			}
			return result;
		}

		// Parse response: { "code": 0, "data": { "user_id": 1 } }
		try
		{
			auto responseJson = json::parse(httpRes.body);

			// Check for error code
			if (responseJson.contains("code") && responseJson["code"].get<int>() != 0)
			{
				CString errorMsg;
				if (responseJson.contains("message"))
				{
					errorMsg = StdStringToCString(responseJson["message"].get<std::string>());
				}
				else
				{
					errorMsg.Format(_T("Server returned error code %d"), responseJson["code"].get<int>());
				}
				result.error = errorMsg;
				return result;
			}

			result.success = TRUE;

			// If registration also returns a token, save it
			if (responseJson.contains("data") && responseJson["data"].contains("device_token"))
			{
				const auto& data = responseJson["data"];
				result.deviceToken = StdStringToCString(data["device_token"].get<std::string>());
				if (data.contains("device_id"))
				{
					result.deviceId = StdStringToCString(data["device_id"].get<std::string>());
					CT2A deviceIdA(result.deviceId, CP_UTF8);
					CGetSetOptions::SetCloudDeviceId(deviceIdA);
				}
				CT2A tokenA(result.deviceToken, CP_UTF8);
				CGetSetOptions::SetCloudDeviceToken(tokenA);

				// Kept in step with Login: an access token without its refresh
				// counterpart dies as soon as it expires.
				if (data.contains("refresh_token"))
				{
					result.refreshToken = StdStringToCString(data["refresh_token"].get<std::string>());
					CT2A refreshA(result.refreshToken, CP_UTF8);
					CGetSetOptions::SetCloudRefreshToken(refreshA);
				}
			}
		}
		catch (const json::parse_error& e)
		{
			result.error.Format(_T("JSON parse error: %hs"), e.what());
		}
		catch (const std::exception& e)
		{
			result.error.Format(_T("Error: %hs"), e.what());
		}
	}
	catch (const std::exception& e)
	{
		result.error.Format(_T("Exception: %hs"), e.what());
	}

	return result;
}

BOOL CCloudAuth::IsLoggedIn()
{
	CStringA token = CGetSetOptions::GetCloudDeviceToken();
	return !token.IsEmpty();
}

void CCloudAuth::Logout()
{
	CGetSetOptions::SetCloudDeviceToken("");
	CGetSetOptions::SetCloudRefreshToken("");
}

// TryRefreshToken - Exchange the stored refresh token for a new access/refresh
// pair. The server returns the pair in the body only when asked with
// ?as=bearer, since this client keeps no cookie jar.
//
// The sync thread and the WS thread can both see a 401 at the same moment, and
// the server rotates the token version on every refresh: two concurrent calls
// would make the second one fail, and its caller would then clear the token the
// first one had just stored. Serialising here removes both races.
BOOL CCloudAuth::TryRefreshToken()
{
	static std::mutex refreshMutex;
	std::lock_guard<std::mutex> lock(refreshMutex);
	return TryRefreshTokenLocked();
}

BOOL CCloudAuth::TryRefreshTokenLocked()
{
	try
	{
		CStringA refreshToken = CGetSetOptions::GetCloudRefreshToken();
		if (refreshToken.IsEmpty())
			return FALSE;

		CString serverUrl = CGetSetOptions::GetCloudServerUrl();
		if (serverUrl.IsEmpty())
			serverUrl = m_httpClientUrl;	// reuse whatever the last login used
		if (serverUrl.IsEmpty())
			return FALSE;

		EnsureHttpClient(serverUrl);
		if (!m_testClient && !m_httpClient)
			return FALSE;

		HttpResult httpRes = DoPostWithBearer(
			"/api/v1/auth/refresh?as=bearer", "{}", "application/json",
			std::string(refreshToken.GetString()));
		if (!httpRes.success || httpRes.status != 200)
			return FALSE;

		auto responseJson = json::parse(httpRes.body);
		if (!responseJson.contains("code") || responseJson["code"].get<int>() != 0)
			return FALSE;
		if (!responseJson.contains("data") || !responseJson["data"].contains("device_token"))
			return FALSE;

		const auto& data = responseJson["data"];
		CString newToken = StdStringToCString(data["device_token"].get<std::string>());
		if (newToken.IsEmpty())
			return FALSE;
		CT2A tokenA(newToken, CP_UTF8);
		CGetSetOptions::SetCloudDeviceToken(tokenA);

		// The server rotates the refresh token as well: the old one is dead once
		// the token version is bumped, so it must be replaced in the same step.
		if (data.contains("refresh_token"))
		{
			CString newRefresh = StdStringToCString(data["refresh_token"].get<std::string>());
			if (!newRefresh.IsEmpty())
			{
				CT2A refreshA(newRefresh, CP_UTF8);
				CGetSetOptions::SetCloudRefreshToken(refreshA);
			}
		}

		OutputDebugStringA("[CloudAuth] access token refreshed\n");
		return TRUE;
	}
	catch (const std::exception&)
	{
		// e.what() can quote part of the response body, which carries tokens.
		OutputDebugStringA("[CloudAuth] refresh failed\n");
		return FALSE;
	}
}

void CCloudAuth::SetHttpClientForTest(std::shared_ptr<IHttpClient> mockClient)
{
	m_testClient = std::move(mockClient);
}

void CCloudAuth::ResetHttpClientForTest()
{
	m_testClient.reset();
}
