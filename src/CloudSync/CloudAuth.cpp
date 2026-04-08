#include "stdafx.h"
#include "CloudAuth.h"
#include "../httplib.h"
#include "../json.hpp"
#include "../Options.h"

using json = nlohmann::json;

// Helper: convert CString to std::string
static std::string CStringToStdString(const CString& str)
{
	CT2A utf8(str, CP_UTF8);
	return std::string(utf8.m_psz);
}

// Helper: convert std::string to CString
static CString StdStringToCString(const std::string& str)
{
	return CString(str.c_str());
}

// Helper: parse URL into host/port and set SSL client
static void SetupClient(const std::string& serverUrl, httplib::Client& cli)
{
	cli.set_connection_timeout(10, 0);
	cli.set_read_timeout(30, 0);
	cli.set_write_timeout(30, 0);
	(void)serverUrl;
}

LoginResult CCloudAuth::Login(const CString& serverUrl,
                              const CString& username,
                              const CString& password)
{
	LoginResult result = {};
	result.success = FALSE;

	try
	{
		std::string url = CStringToStdString(serverUrl);
		httplib::Client cli(url);
		SetupClient(url, cli);

		json body;
		body["username"] = CStringToStdString(username);
		body["password"] = CStringToStdString(password);

		std::string bodyStr = body.dump();
		auto res = cli.Post("/api/v1/auth/login", bodyStr, "application/json");
		if (!res)
		{
			result.error = _T("Failed to connect to server (network error)");
			return result;
		}

		if (res->status != 200)
		{
			try
			{
				auto errJson = json::parse(res->body);
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
					result.error.Format(_T("Server returned HTTP %d"), res->status);
				}
			}
			catch (const json::parse_error& e)
			{
				CString msg;
				msg.Format(_T("HTTP %d, parse error: %hs"), res->status, e.what());
				result.error = msg;
			}
			catch (...)
			{
				result.error.Format(_T("Server returned HTTP %d"), res->status);
			}
			return result;
		}

		// Parse response: { "code": 0, "data": { "device_token": "...", "device_id": "..." } }
		try
		{
			auto responseJson = json::parse(res->body);

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
		std::string url = CStringToStdString(serverUrl);
		httplib::Client cli(url);
		SetupClient(url, cli);

		json body;
		body["username"] = CStringToStdString(username);
		body["email"] = CStringToStdString(email);
		body["password"] = CStringToStdString(password);

		std::string bodyStr = body.dump();
		auto res = cli.Post("/api/v1/auth/register", bodyStr, "application/json");
		if (!res)
		{
			result.error = _T("Failed to connect to server (network error)");
			return result;
		}

		if (res->status != 200 && res->status != 201)
		{
			try
			{
				auto errJson = json::parse(res->body);
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
					result.error.Format(_T("Server returned HTTP %d"), res->status);
				}
			}
			catch (const json::parse_error& e)
			{
				CString msg;
				msg.Format(_T("HTTP %d, parse error: %hs"), res->status, e.what());
				result.error = msg;
			}
			catch (...)
			{
				result.error.Format(_T("Server returned HTTP %d"), res->status);
			}
			return result;
		}

		// Parse response: { "code": 0, "data": { "user_id": 1 } }
		try
		{
			auto responseJson = json::parse(res->body);

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
}
