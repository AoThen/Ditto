#include "stdafx.h"
#include "CloudEncryption.h"
#include "../httplib.h"
#include "../json.hpp"
#include "../Options.h"

using json = nlohmann::json;

// Static member definitions
std::unique_ptr<httplib::Client> CCloudEncryption::m_httpClient;
CString CCloudEncryption::m_httpClientUrl;

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

void CCloudEncryption::EnsureHttpClient(const CString& serverUrl, const CString& deviceToken)
{
	std::string url = CStringToStdString(serverUrl);
	if (!m_httpClient || m_httpClientUrl != serverUrl)
	{
		m_httpClient = std::make_unique<httplib::Client>(url);
		m_httpClient->set_connection_timeout(10, 0);
		m_httpClient->set_read_timeout(30, 0);
		m_httpClient->set_write_timeout(30, 0);
		{
			httplib::Headers headers;
			headers.emplace("Authorization", "Bearer " + CStringToStdString(deviceToken));
			m_httpClient->set_default_headers(headers);
		}
		m_httpClientUrl = serverUrl;
	}
}

static CString StdStringToCString(const std::string& str)
{
	if (str.empty())
		return CString();
	CA2W wide(str.c_str(), CP_UTF8);
	return CString(wide);
}

static CStringA ComputeVerificationHashForPassword(const CString& password, const CStringA& saltB64)
{
	CT2A passwordUtf8(password, CP_UTF8);
	return CCloudCrypto::ComputeVerificationHash(CStringA(passwordUtf8), saltB64);
}

EncryptionSetupResult CCloudEncryption::SetupEncryption(
	const CString& serverUrl,
	const CString& deviceToken,
	const CString& password)
{
	EncryptionSetupResult result = {};
	result.success = FALSE;

	try
	{
		EnsureHttpClient(serverUrl, deviceToken);
		if (!m_httpClient)
		{
			result.error = _T("Failed to create HTTP client");
			return result;
		}

		auto saltRes = m_httpClient->Get("/api/v1/encryption/salt");
		if (!saltRes || saltRes->status != 200)
		{
			result.error = _T("Failed to get encryption salt from server");
			return result;
		}

		auto saltJson = json::parse(saltRes->body);
		if (!saltJson.contains("data") || !saltJson["data"].contains("salt"))
		{
			result.error = _T("Invalid server response");
			return result;
		}

		CStringA saltB64(saltJson["data"]["salt"].get<std::string>().c_str());
		std::vector<BYTE> saltBytes = CCloudCrypto::Base64Decode(saltB64);

		std::vector<BYTE> dek = CCloudCrypto::RandomBytes(32);

		CT2A passwordUtf8(password, CP_UTF8);
		CStringA passwordA(passwordUtf8);
		std::vector<BYTE> kek = CCloudCrypto::DeriveKey(passwordA, saltBytes, 100000);

		CStringA wrappedDEK = CCloudCrypto::WrapKey(kek, dek);
		if (wrappedDEK.IsEmpty())
		{
			result.error = _T("Key wrapping failed");
			return result;
		}

		CStringA verificationHash = ComputeVerificationHashForPassword(password, saltB64);
		if (verificationHash.IsEmpty())
		{
			result.error = _T("Verification hash calculation failed");
			return result;
		}

		json body;
		body["wrapped_dek"] = wrappedDEK.GetString();
		body["verification_hash"] = verificationHash.GetString();
		body["password_hint"] = "";

		auto setupRes = m_httpClient->Post("/api/v1/encryption/setup", body.dump(), "application/json");
		if (!setupRes)
		{
			result.error = _T("Failed to connect to server (network error)");
			return result;
		}

		if (setupRes->status != 200 && setupRes->status != 201)
		{
			try
			{
				auto errJson = json::parse(setupRes->body);
				if (errJson.contains("message"))
					result.error = StdStringToCString(errJson["message"].get<std::string>());
				else
					result.error.Format(_T("Server returned HTTP %d"), setupRes->status);
			}
			catch (...)
			{
				result.error.Format(_T("Server returned HTTP %d"), setupRes->status);
			}
			return result;
		}

		auto responseJson = json::parse(setupRes->body);
		if (responseJson.contains("code") && responseJson["code"].get<int>() != 0)
		{
			if (responseJson.contains("message"))
				result.error = StdStringToCString(responseJson["message"].get<std::string>());
			return result;
		}

		if (!responseJson.contains("data"))
		{
			result.error = _T("Invalid server response");
			return result;
		}

		const auto& data = responseJson["data"];
		result.salt = StdStringToCString(data["salt"].get<std::string>());
		result.encryptionEnabled = data.value("encryption_enabled", false);

		if (!CCloudCrypto::Initialize(dek))
		{
			result.error = _T("Encryption initialization failed");
			return result;
		}

		CStringA dekB64 = CCloudCrypto::Base64Encode(dek);
		CGetSetOptions::SetCloudSyncEncryptionEnabled(TRUE);
		CGetSetOptions::SetCloudEncryptionKey(CString(dekB64));
		CGetSetOptions::SetCloudEncryptionSalt(result.salt);

		result.success = TRUE;
	}
	catch (const json::parse_error& e)
	{
		result.error.Format(_T("JSON parse error: %hs"), e.what());
	}
	catch (const std::exception& e)
	{
		result.error.Format(_T("Error: %hs"), e.what());
	}

	return result;
}

EncryptionStatusResult CCloudEncryption::GetEncryptionStatus(
	const CString& serverUrl,
	const CString& deviceToken)
{
	EncryptionStatusResult result = {};
	result.success = FALSE;

	try
	{
		EnsureHttpClient(serverUrl, deviceToken);
		if (!m_httpClient)
		{
			result.error = _T("Failed to create HTTP client");
			return result;
		}

		auto res = m_httpClient->Get("/api/v1/encryption/salt");
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
				if (errJson.contains("message"))
					result.error = StdStringToCString(errJson["message"].get<std::string>());
				else
					result.error.Format(_T("Server returned HTTP %d"), res->status);
			}
			catch (...)
			{
				result.error.Format(_T("Server returned HTTP %d"), res->status);
			}
			return result;
		}

		auto responseJson = json::parse(res->body);
		if (responseJson.contains("code") && responseJson["code"].get<int>() != 0)
		{
			if (responseJson.contains("message"))
				result.error = StdStringToCString(responseJson["message"].get<std::string>());
			return result;
		}

		if (!responseJson.contains("data"))
		{
			result.error = _T("Invalid server response");
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
		result.error.Format(_T("JSON parse error: %hs"), e.what());
	}
	catch (const std::exception& e)
	{
		result.error.Format(_T("Error: %hs"), e.what());
	}

	return result;
}

ChangePasswordResult CCloudEncryption::ChangeEncryptionPassword(
	const CString& serverUrl,
	const CString& deviceToken,
	const CString& oldPassword,
	const CString& newPassword)
{
	ChangePasswordResult result = {};
	result.success = FALSE;

	try
	{
		EnsureHttpClient(serverUrl, deviceToken);
		if (!m_httpClient)
		{
			result.error = _T("Failed to create HTTP client");
			return result;
		}

		auto keyRes = m_httpClient->Get("/api/v1/encryption/key-material");
		if (!keyRes || keyRes->status != 200)
		{
			result.error = _T("Failed to get key material from server");
			return result;
		}

		auto keyJson = json::parse(keyRes->body);
		if (!keyJson.contains("data") || !keyJson["data"].contains("wrapped_dek") || !keyJson["data"].contains("salt"))
		{
			result.error = _T("Invalid server response: missing key material");
			return result;
		}

		CStringA wrappedDEKB64(keyJson["data"]["wrapped_dek"].get<std::string>().c_str());
		CStringA oldSaltB64(keyJson["data"]["salt"].get<std::string>().c_str());

		std::vector<BYTE> oldSaltBytes = CCloudCrypto::Base64Decode(oldSaltB64);

		CT2A oldPasswordUtf8(oldPassword, CP_UTF8);
		CStringA oldPasswordA(oldPasswordUtf8);
		std::vector<BYTE> oldKEK = CCloudCrypto::DeriveKey(oldPasswordA, oldSaltBytes, 100000);

		std::vector<BYTE> dek = CCloudCrypto::UnwrapKey(oldKEK, wrappedDEKB64);
		if (dek.empty())
		{
			result.error = _T("Old password verification failed, unable to unwrap key");
			return result;
		}

		std::vector<BYTE> newSaltBytes = CCloudCrypto::RandomBytes(32);
		CStringA newSaltB64 = CCloudCrypto::Base64Encode(newSaltBytes);

		CT2A newPasswordUtf8(newPassword, CP_UTF8);
		CStringA newPasswordA(newPasswordUtf8);
		std::vector<BYTE> newKEK = CCloudCrypto::DeriveKey(newPasswordA, newSaltBytes, 100000);

		CStringA newWrappedDEK = CCloudCrypto::WrapKey(newKEK, dek);
		if (newWrappedDEK.IsEmpty())
		{
			result.error = _T("New key wrapping failed");
			return result;
		}

		CStringA oldVerificationHash = ComputeVerificationHashForPassword(oldPassword, oldSaltB64);
		CStringA newVerificationHash = ComputeVerificationHashForPassword(newPassword, newSaltB64);

		json body;
		body["old_verification_hash"] = oldVerificationHash.GetString();
		body["new_salt"] = newSaltB64.GetString();
		body["new_wrapped_dek"] = newWrappedDEK.GetString();
		body["new_verification_hash"] = newVerificationHash.GetString();
		body["new_password_hint"] = "";

		auto changeRes = m_httpClient->Post("/api/v1/encryption/change-password", body.dump(), "application/json");
		if (!changeRes)
		{
			result.error = _T("Failed to connect to server (network error)");
			return result;
		}

		if (changeRes->status != 200)
		{
			try
			{
				auto errJson = json::parse(changeRes->body);
				if (errJson.contains("message"))
					result.error = StdStringToCString(errJson["message"].get<std::string>());
				else
					result.error.Format(_T("Server returned HTTP %d"), changeRes->status);
			}
			catch (...)
			{
				result.error.Format(_T("Server returned HTTP %d"), changeRes->status);
			}
			return result;
		}

		auto responseJson = json::parse(changeRes->body);
		if (responseJson.contains("code") && responseJson["code"].get<int>() != 0)
		{
			if (responseJson.contains("message"))
				result.error = StdStringToCString(responseJson["message"].get<std::string>());
			return result;
		}

		if (responseJson.contains("data") && responseJson["data"].contains("salt"))
		{
			result.newSalt = StdStringToCString(responseJson["data"]["salt"].get<std::string>());
			CGetSetOptions::SetCloudEncryptionSalt(result.newSalt);
		}

		result.success = TRUE;
	}
	catch (const json::parse_error& e)
	{
		result.error.Format(_T("JSON parse error: %hs"), e.what());
	}
	catch (const std::exception& e)
	{
		result.error.Format(_T("Error: %hs"), e.what());
	}

	return result;
}

// ---------------------------------------------------------------------------
// ReVerifyPassword: Verify password by fetching key-material from server,
// deriving KEK, and unwrapping DEK. Used when salt changes on server
// (password changed on another device). On success, stores DEK and new salt locally.
// ---------------------------------------------------------------------------
EncryptionSetupResult CCloudEncryption::ReVerifyPassword(
	const CString& serverUrl,
	const CString& deviceToken,
	const CString& password)
{
	EncryptionSetupResult result = {};
	result.success = FALSE;

	try
	{
		EnsureHttpClient(serverUrl, deviceToken);
		if (!m_httpClient)
		{
			result.error = _T("Failed to create HTTP client");
			return result;
		}

		auto keyRes = m_httpClient->Get("/api/v1/encryption/key-material");
		if (!keyRes)
		{
			result.error = _T("Failed to connect to server (network error)");
			return result;
		}

		if (keyRes->status != 200)
		{
			try
			{
				auto errJson = json::parse(keyRes->body);
				if (errJson.contains("message"))
					result.error = StdStringToCString(errJson["message"].get<std::string>());
				else
					result.error.Format(_T("Server returned HTTP %d"), keyRes->status);
			}
			catch (...)
			{
				result.error.Format(_T("Server returned HTTP %d"), keyRes->status);
			}
			return result;
		}

		auto keyJson = json::parse(keyRes->body);
		if (!keyJson.contains("data") || !keyJson["data"].contains("wrapped_dek") || !keyJson["data"].contains("salt"))
		{
			result.error = _T("Invalid server response: missing key material");
			return result;
		}

		CStringA wrappedDEKB64(keyJson["data"]["wrapped_dek"].get<std::string>().c_str());
		CStringA serverSaltB64(keyJson["data"]["salt"].get<std::string>().c_str());

		std::vector<BYTE> saltBytes = CCloudCrypto::Base64Decode(serverSaltB64);

		CT2A passwordUtf8(password, CP_UTF8);
		CStringA passwordA(passwordUtf8);
		std::vector<BYTE> kek = CCloudCrypto::DeriveKey(passwordA, saltBytes, 100000);

		std::vector<BYTE> dek = CCloudCrypto::UnwrapKey(kek, wrappedDEKB64);
		if (dek.empty())
		{
			result.error = _T("Password verification failed. Please enter the correct encryption password.");
			return result;
		}

		if (!CCloudCrypto::Initialize(dek))
		{
			result.error = _T("Failed to initialize crypto with recovered key");
			return result;
		}

		CStringA dekB64 = CCloudCrypto::Base64Encode(dek);
		CGetSetOptions::SetCloudEncryptionKey(CString(dekB64));

		CString serverSalt = StdStringToCString(keyJson["data"]["salt"].get<std::string>());
		CGetSetOptions::SetCloudEncryptionSalt(serverSalt);

		result.salt = serverSalt;
		result.encryptionEnabled = TRUE;
		result.success = TRUE;
	}
	catch (const json::parse_error& e)
	{
		result.error.Format(_T("JSON parse error: %hs"), e.what());
	}
	catch (const std::exception& e)
	{
		result.error.Format(_T("Error: %hs"), e.what());
	}

	return result;
}

BOOL CCloudEncryption::CheckSaltChanged(
	const CString& serverUrl,
	const CString& deviceToken)
{
	try
	{
		BOOL enabled = CGetSetOptions::GetCloudSyncEncryptionEnabled();
		if (!enabled)
			return FALSE;

		CString localSalt = CGetSetOptions::GetCloudEncryptionSalt();
		if (localSalt.IsEmpty())
			return FALSE;

		EnsureHttpClient(serverUrl, deviceToken);
		if (!m_httpClient)
			return FALSE;

		auto res = m_httpClient->Get("/api/v1/encryption/salt");
		if (!res || res->status != 200)
			return FALSE;

		auto responseJson = json::parse(res->body);
		if (!responseJson.contains("data") || !responseJson["data"].contains("salt"))
			return FALSE;

		CString serverSalt = StdStringToCString(responseJson["data"]["salt"].get<std::string>());

		return (localSalt != serverSalt);
	}
	catch (...)
	{
		return FALSE;
	}
}

BOOL CCloudEncryption::InitializeCryptoFromStoredKey()
{
	try
	{
		BOOL enabled = CGetSetOptions::GetCloudSyncEncryptionEnabled();
		if (!enabled)
		{
			OutputDebugStringA("[CloudEncryption] Encryption not enabled.\n");
			return FALSE;
		}

		CStringA dekB64 = CGetSetOptions::GetCloudEncryptionKey();
		if (dekB64.IsEmpty())
		{
			OutputDebugStringA("[CloudEncryption] No stored key found.\n");
			return FALSE;
		}

		std::vector<BYTE> keyBytes = CCloudCrypto::Base64Decode(dekB64);
		if (keyBytes.empty() || keyBytes.size() != 32)
		{
			OutputDebugStringA("[CloudEncryption] Invalid key size.\n");
			return FALSE;
		}

		BOOL ok = CCloudCrypto::Initialize(keyBytes);
		if (ok)
		{
			OutputDebugStringA("[CloudEncryption] Crypto initialized from stored DEK.\n");
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

CStringA CCloudEncryption::EncryptClipData(const CStringA& plaintext)
{
	if (!IsEncryptionReady())
	{
		OutputDebugStringA("[CloudEncryption] EncryptClipData: encryption not ready, skipping encryption.\n");
		return CStringA("");
	}
	return CCloudCrypto::Encrypt(plaintext);
}

CStringA CCloudEncryption::DecryptClipData(const CStringA& encryptedBase64)
{
	if (encryptedBase64.IsEmpty())
		return CStringA("");

	if (!IsEncryptionReady())
	{
		return encryptedBase64;
	}

	CStringA result = CCloudCrypto::Decrypt(encryptedBase64);
	return result;
}

BOOL CCloudEncryption::IsEncryptionReady()
{
	BOOL enabled = CGetSetOptions::GetCloudSyncEncryptionEnabled();
	return enabled;
}