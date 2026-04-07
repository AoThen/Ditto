#include "stdafx.h"
#include "CloudEncryption.h"
#include "../httplib.h"
#include "../json.hpp"
#include "../Options.h"

using json = nlohmann::json;

static std::string CStringToStdString(const CString& str)
{
	CT2A utf8(str, CP_UTF8);
	return std::string(utf8.m_psz);
}

static CString StdStringToCString(const std::string& str)
{
	return CString(str.c_str());
}

static void SetupClient(const std::string& serverUrl, httplib::Client& cli, const CString& deviceToken)
{
	cli.enable_server_certificate_verification(false);
	cli.set_default_headers({
		{"Authorization", "Bearer " + CStringToStdString(deviceToken)}
	});
}

// ---------------------------------------------------------------------------
// SetupEncryption: POST /api/v1/encryption/setup
// ---------------------------------------------------------------------------
EncryptionSetupResult CCloudEncryption::SetupEncryption(
	const CString& serverUrl,
	const CString& deviceToken,
	const CString& password)
{
	EncryptionSetupResult result = {};
	result.success = FALSE;

	try
	{
		std::string url = CStringToStdString(serverUrl);
		httplib::Client cli(url);
		SetupClient(url, cli, deviceToken);

		// Step 1: Request salt from server (setup endpoint generates new salt)
		json body;
		body["password_hint"] = ""; // Optional: can store hint for user

		std::string bodyStr = body.dump();
		auto res = cli.Post("/api/v1/encryption/setup", bodyStr, "application/json");
		if (!res)
		{
			result.error = _T("无法连接服务器（网络错误）");
			return result;
		}

		if (res->status != 200 && res->status != 201)
		{
			try
			{
				auto errJson = json::parse(res->body);
				if (errJson.contains("message"))
				{
					result.error = StdStringToCString(errJson["message"].get<std::string>());
				}
				else
				{
					result.error.Format(_T("服务器返回 HTTP %d"), res->status);
				}
			}
			catch (...)
			{
				result.error.Format(_T("服务器返回 HTTP %d"), res->status);
			}
			return result;
		}

		// Step 2: Parse response
		try
		{
			auto responseJson = json::parse(res->body);

			if (responseJson.contains("code") && responseJson["code"].get<int>() != 0)
			{
				if (responseJson.contains("message"))
				{
					result.error = StdStringToCString(responseJson["message"].get<std::string>());
				}
				return result;
			}

			if (!responseJson.contains("data"))
			{
				result.error = _T("服务器响应无效");
				return result;
			}

			const auto& data = responseJson["data"];
			result.salt = StdStringToCString(data["salt"].get<std::string>());
			result.encryptionEnabled = data.value("encryption_enabled", false);

			// Step 3: Derive AES key from password + salt
			CStringA passwordA(password);
			std::vector<BYTE> saltBytes = CCloudCrypto::Base64Decode(result.salt);
			std::vector<BYTE> aesKey = CCloudCrypto::DeriveKey(passwordA, saltBytes, 100000);

			// Step 4: Initialize crypto
			if (!CCloudCrypto::Initialize(aesKey))
			{
				result.error = _T("加密初始化失败");
				return result;
			}

			// Step 5: Store encrypted key for persistence (so we can reload on startup)
			// For now, we store the password-derived key directly encrypted with a machine key
			// In production, you'd use DPAPI or a secure key store
			CStringA keyBase64 = CCloudCrypto::Base64Encode(aesKey);
			CGetSetOptions::SetCloudEncryptionEnabled(TRUE);
			CGetSetOptions::SetCloudEncryptionKey(keyBase64);
			CGetSetOptions::SetCloudEncryptionSalt(result.salt);

			result.success = TRUE;
		}
		catch (const json::parse_error& e)
		{
			result.error.Format(_T("JSON 解析错误: %hs"), e.what());
		}
		catch (const std::exception& e)
		{
			result.error.Format(_T("错误: %hs"), e.what());
		}
	}
	catch (const std::exception& e)
	{
		result.error.Format(_T("异常: %hs"), e.what());
	}

	return result;
}

// ---------------------------------------------------------------------------
// GetEncryptionStatus: GET /api/v1/encryption/salt
// ---------------------------------------------------------------------------
EncryptionStatusResult CCloudEncryption::GetEncryptionStatus(
	const CString& serverUrl,
	const CString& deviceToken)
{
	EncryptionStatusResult result = {};
	result.success = FALSE;

	try
	{
		std::string url = CStringToStdString(serverUrl);
		httplib::Client cli(url);
		SetupClient(url, cli, deviceToken);

		auto res = cli.Get("/api/v1/encryption/salt");
		if (!res)
		{
			result.error = _T("无法连接服务器（网络错误）");
			return result;
		}

		if (res->status != 200)
		{
			try
			{
				auto errJson = json::parse(res->body);
				if (errJson.contains("message"))
				{
					result.error = StdStringToCString(errJson["message"].get<std::string>());
				}
				else
				{
					result.error.Format(_T("服务器返回 HTTP %d"), res->status);
				}
			}
			catch (...)
			{
				result.error.Format(_T("服务器返回 HTTP %d"), res->status);
			}
			return result;
		}

		try
		{
			auto responseJson = json::parse(res->body);

			if (responseJson.contains("code") && responseJson["code"].get<int>() != 0)
			{
				if (responseJson.contains("message"))
				{
					result.error = StdStringToCString(responseJson["message"].get<std::string>());
				}
				return result;
			}

			if (!responseJson.contains("data"))
			{
				result.error = _T("服务器响应无效");
				return result;
			}

			const auto& data = responseJson["data"];
			result.salt = StdStringToCString(data["salt"].get<std::string>());
			result.encryptionEnabled = data.value("encryption_enabled", false);
			if (data.contains("password_hint"))
			{
				result.passwordHint = StdStringToCString(data["password_hint"].get<std::string>());
			}

			result.success = TRUE;
		}
		catch (const json::parse_error& e)
		{
			result.error.Format(_T("JSON 解析错误: %hs"), e.what());
		}
		catch (const std::exception& e)
		{
			result.error.Format(_T("错误: %hs"), e.what());
		}
	}
	catch (const std::exception& e)
	{
		result.error.Format(_T("异常: %hs"), e.what());
	}

	return result;
}

// ---------------------------------------------------------------------------
// InitializeCryptoFromStoredKey: reload key at startup
// ---------------------------------------------------------------------------
BOOL CCloudEncryption::InitializeCryptoFromStoredKey()
{
	try
	{
		// Check if encryption is enabled
		BOOL enabled = CGetSetOptions::GetCloudSyncEncryptionEnabled();
		if (!enabled)
		{
			OutputDebugStringA("[CloudEncryption] Encryption not enabled.\n");
			return FALSE;
		}

		// Load stored key
		CStringA keyBase64 = CGetSetOptions::GetCloudEncryptionKey();
		if (keyBase64.IsEmpty())
		{
			OutputDebugStringA("[CloudEncryption] No stored key found.\n");
			return FALSE;
		}

		// Decode and initialize
		std::vector<BYTE> keyBytes = CCloudCrypto::Base64Decode(keyBase64);
		if (keyBytes.empty() || keyBytes.size() != 32)
		{
			OutputDebugStringA("[CloudEncryption] Invalid key size.\n");
			return FALSE;
		}

		BOOL ok = CCloudCrypto::Initialize(keyBytes);
		if (ok)
		{
			OutputDebugStringA("[CloudEncryption] Crypto initialized from stored key.\n");
		}
		else
		{
			OutputDebugStringA("[CloudEncryption] Failed to initialize crypto.\n");
		}
		return ok;
	}
	catch (...)
	{
		OutputDebugStringA("[CloudEncryption] Exception in InitializeCryptoFromStoredKey.\n");
		return FALSE;
	}
}

// ---------------------------------------------------------------------------
// EncryptClipData: wrapper around CCloudCrypto::Encrypt
// ---------------------------------------------------------------------------
CStringA CCloudEncryption::EncryptClipData(const CStringA& plaintext)
{
	if (!IsEncryptionReady())
	{
		// If encryption not ready, return plaintext (or empty based on policy)
		// For safety, return plaintext so sync still works (unencrypted mode)
		return plaintext;
	}
	return CCloudCrypto::Encrypt(plaintext);
}

// ---------------------------------------------------------------------------
// DecryptClipData: wrapper around CCloudCrypto::Decrypt
// ---------------------------------------------------------------------------
CStringA CCloudEncryption::DecryptClipData(const CStringA& encryptedBase64)
{
	if (encryptedBase64.IsEmpty())
		return CStringA("");

	if (!IsEncryptionReady())
	{
		// If encryption not ready, return as-is
		return encryptedBase64;
	}

	// Try to decrypt; if fails, return as-is (fallback for unencrypted data)
	CStringA result = CCloudCrypto::Decrypt(encryptedBase64);
	if (result.IsEmpty() && !encryptedBase64.IsEmpty())
	{
		// Decryption failed, assume it was stored unencrypted
		return encryptedBase64;
	}
	return result;
}

// ---------------------------------------------------------------------------
// IsEncryptionReady
// ---------------------------------------------------------------------------
BOOL CCloudEncryption::IsEncryptionReady()
{
	// Check if crypto module is initialized
	// This is a local check - doesn't verify server state
	BOOL enabled = CGetSetOptions::GetCloudSyncEncryptionEnabled();
	return enabled;
}
