#include "stdafx.h"
#include "OptionCloud.h"
#include "CloudAuth.h"
#include "CloudCrypto.h"
#include "../httplib.h"
#include "../json.hpp"
#include "../Options.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

IMPLEMENT_DYNCREATE(COptionCloud, CPropertyPage)

COptionCloud::COptionCloud()
	: CPropertyPage(COptionCloud::IDD)
	, m_bEnabled(FALSE)
	, m_bAutoSync(TRUE)
	, m_csServerUrl(_T("https://localhost:8080"))
	, m_csUsername(_T(""))
	, m_csPassword(_T(""))
	, m_csStatus(_T(""))
	, m_csEncryptionPassword(_T(""))
	, m_csEncryptionStatus(_T(""))
	, m_bEncryptionEnabled(FALSE)
{
	// Set property sheet title to "Cloud Sync" (Chinese: 云端同步)
	m_psp.pszTitle = _T("云端同步");
	m_psp.dwFlags |= PSP_USETITLE;
}

COptionCloud::~COptionCloud()
{
}

void COptionCloud::DoDataExchange(CDataExchange* pDX)
{
	CPropertyPage::DoDataExchange(pDX);
	DDX_Check(pDX, IDC_CLOUD_ENABLE, m_bEnabled);
	DDX_Check(pDX, IDC_CLOUD_AUTO_SYNC, m_bAutoSync);
	DDX_Text(pDX, IDC_CLOUD_SERVER_URL, m_csServerUrl);
	DDX_Text(pDX, IDC_CLOUD_USERNAME, m_csUsername);
	DDX_Text(pDX, IDC_CLOUD_PASSWORD, m_csPassword);
	DDX_Text(pDX, IDC_CLOUD_STATUS, m_csStatus);
}

BEGIN_MESSAGE_MAP(COptionCloud, CPropertyPage)
	ON_BN_CLICKED(IDC_CLOUD_BTN_LOGIN, &COptionCloud::OnBtnLogin)
	ON_BN_CLICKED(IDC_CLOUD_BTN_REGISTER, &COptionCloud::OnBtnRegister)
	ON_BN_CLICKED(IDC_CLOUD_BTN_ENABLE_ENCRYPTION, &COptionCloud::OnBtnEnableEncryption)
	ON_BN_CLICKED(IDC_CLOUD_BTN_TEST_ENCRYPTION, &COptionCloud::OnBtnTestEncryption)
END_MESSAGE_MAP()

BOOL COptionCloud::OnInitDialog()
{
	CPropertyPage::OnInitDialog();

	// Load saved values from CGetSetOptions
	m_bEnabled = CGetSetOptions::GetCloudSyncEnabled();
	m_bAutoSync = CGetSetOptions::GetCloudAutoSync();
	m_csServerUrl = CGetSetOptions::GetCloudServerUrl();
	if (m_csServerUrl.IsEmpty())
	{
		m_csServerUrl = _T("https://localhost:8080");
	}

	CStringA token = CGetSetOptions::GetCloudDeviceToken();
	if (!token.IsEmpty())
	{
		m_csStatus = _T("Logged in (token present)");
	}
	else
	{
		m_csStatus = _T("Not logged in");
	}

	// Check encryption status
	CString csKeyB64 = CGetSetOptions::GetCloudEncryptionKey();
	if (!csKeyB64.IsEmpty())
	{
		m_bEncryptionEnabled = TRUE;
		m_csEncryptionStatus = _T("Encryption is enabled.");
	}
	else
	{
		m_csEncryptionStatus = _T("Encryption is not enabled.");
	}

	UpdateData(FALSE);

	return TRUE;  // return TRUE unless you set the focus to a control
}

BOOL COptionCloud::OnApply()
{
	// Save values via CGetSetOptions
	CGetSetOptions::SetCloudSyncEnabled(m_bEnabled);
	CGetSetOptions::SetCloudAutoSync(m_bAutoSync);
	CT2A urlA(m_csServerUrl, CP_UTF8);
	CGetSetOptions::SetCloudServerUrl(m_csServerUrl);

	return CPropertyPage::OnApply();
}

void COptionCloud::OnBtnLogin()
{
	// Validate fields
	if (m_csServerUrl.IsEmpty())
	{
		MessageBox(_T("Please enter a server URL."), _T("Login"), MB_ICONWARNING);
		return;
	}

	if (m_csUsername.IsEmpty())
	{
		MessageBox(_T("Please enter a username."), _T("Login"), MB_ICONWARNING);
		return;
	}

	if (m_csPassword.IsEmpty())
	{
		MessageBox(_T("Please enter a password."), _T("Login"), MB_ICONWARNING);
		return;
	}

	// Show status
	m_csStatus = _T("登录中...");
	UpdateData(FALSE);

	try
	{
		LoginResult result = CCloudAuth::Login(m_csServerUrl, m_csUsername, m_csPassword);

		if (result.success)
		{
			m_csStatus = _T("Logged in successfully.");
			MessageBox(_T("Login successful."), _T("Login"), MB_ICONINFORMATION);
		}
		else
		{
			m_csStatus = result.error;
			CString msg;
			msg.Format(_T("Login failed: %s"), result.error.GetString());
			MessageBox(msg, _T("Login"), MB_ICONERROR);
		}
	}
	catch (const std::exception& e)
	{
		m_csStatus.Format(_T("Exception: %hs"), e.what());
		CString msg;
		msg.Format(_T("Login error: %hs"), e.what());
		MessageBox(msg, _T("Login"), MB_ICONERROR);
	}
	catch (...)
	{
		m_csStatus = _T("Unknown error during login.");
		MessageBox(_T("An unknown error occurred during login."), _T("Login"), MB_ICONERROR);
	}

	UpdateData(FALSE);
}

void COptionCloud::OnBtnRegister()
{
	// Validate fields
	if (m_csServerUrl.IsEmpty())
	{
		MessageBox(_T("Please enter a server URL."), _T("Register"), MB_ICONWARNING);
		return;
	}

	if (m_csUsername.IsEmpty())
	{
		MessageBox(_T("Please enter a username."), _T("Register"), MB_ICONWARNING);
		return;
	}

	if (m_csPassword.IsEmpty())
	{
		MessageBox(_T("Please enter a password."), _T("Register"), MB_ICONWARNING);
		return;
	}

	// For registration we need an email -- prompt for it
	CString email = m_csUsername + _T("@example.com");  // Derive email from username

	// Show status
	m_csStatus = _T("注册中...");
	UpdateData(FALSE);

	try
	{
		LoginResult result = CCloudAuth::Register(m_csServerUrl, m_csUsername, email, m_csPassword);

		if (result.success)
		{
			m_csStatus = result.error.IsEmpty() ? _T("Registration successful.") : result.error;
			MessageBox(result.error.IsEmpty() ? _T("Registration successful. Please login.") : result.error,
			           _T("Register"), MB_ICONINFORMATION);
		}
		else
		{
			m_csStatus = result.error;
			CString msg;
			msg.Format(_T("Registration failed: %s"), result.error.GetString());
			MessageBox(msg, _T("Register"), MB_ICONERROR);
		}
	}
	catch (const std::exception& e)
	{
		m_csStatus.Format(_T("Exception: %hs"), e.what());
		CString msg;
		msg.Format(_T("Registration error: %hs"), e.what());
		MessageBox(msg, _T("Register"), MB_ICONERROR);
	}
	catch (...)
	{
		m_csStatus = _T("Unknown error during registration.");
		MessageBox(_T("An unknown error occurred during registration."), _T("Register"), MB_ICONERROR);
	}

	UpdateData(FALSE);
}

// ---------------------------------------------------------------------------
// OnBtnEnableEncryption
// ---------------------------------------------------------------------------
void COptionCloud::OnBtnEnableEncryption()
{
	if (m_csEncryptionPassword.IsEmpty())
	{
		MessageBox(_T("Please enter an encryption password."), _T("Enable Encryption"), MB_ICONWARNING);
		return;
	}

	m_csEncryptionStatus = _T("正在生成加密密钥...");
	UpdateData(FALSE);

	try
	{
		// 1. Get salt from server
		std::string url = CT2A(m_csServerUrl, CP_UTF8).m_psz;
		httplib::Client cli(url);

		auto res = cli.Get("/api/v1/encryption/salt");
		if (!res || res->status != 200)
		{
			m_csEncryptionStatus = _T("Failed to get salt from server. Check server URL and connection.");
			MessageBox(m_csEncryptionStatus, _T("Enable Encryption"), MB_ICONERROR);
			return;
		}

		// Parse salt response: { "code": 0, "data": { "salt": "<base64>" } }
		auto jsonRes = nlohmann::json::parse(res->body);
		if (!jsonRes.contains("data") || !jsonRes["data"].contains("salt"))
		{
			m_csEncryptionStatus = _T("Invalid salt response from server.");
			MessageBox(m_csEncryptionStatus, _T("Enable Encryption"), MB_ICONERROR);
			return;
		}

		std::string saltB64 = jsonRes["data"]["salt"].get<std::string>();
		std::vector<BYTE> salt = CCloudCrypto::Base64Decode(CStringA(saltB64.c_str()));
		if (salt.size() != 32)
		{
			m_csEncryptionStatus = _T("Invalid salt size from server.");
			MessageBox(m_csEncryptionStatus, _T("Enable Encryption"), MB_ICONERROR);
			return;
		}

		// 2. Derive key using PBKDF2-HMAC-SHA256
		CT2A passwordA(m_csEncryptionPassword, CP_UTF8);
		std::vector<BYTE> key = CCloudCrypto::DeriveKey(CStringA(passwordA), salt, 100000);
		if (key.size() != 32)
		{
			m_csEncryptionStatus = _T("Failed to derive encryption key.");
			MessageBox(m_csEncryptionStatus, _T("Enable Encryption"), MB_ICONERROR);
			return;
		}

		// 3. Save key to registry as base64
		CStringA keyB64 = CCloudCrypto::Base64Encode(key);
		CGetSetOptions::SetCloudEncryptionKey(CString(keyB64.GetString()));

		// 4. Initialize CCloudCrypto
		if (CCloudCrypto::Initialize(key))
		{
			m_bEncryptionEnabled = TRUE;
			m_csEncryptionStatus = _T("Encryption enabled successfully.");
			MessageBox(_T("加密已启用。您的数据将在同步前进行加密。"),
			           _T("Enable Encryption"), MB_ICONINFORMATION);
		}
		else
		{
			m_csEncryptionStatus = _T("Failed to initialize encryption.");
			MessageBox(m_csEncryptionStatus, _T("Enable Encryption"), MB_ICONERROR);
		}
	}
	catch (const std::exception& e)
	{
		m_csEncryptionStatus.Format(_T("Exception: %hs"), e.what());
		CString msg;
		msg.Format(_T("Error enabling encryption: %hs"), e.what());
		MessageBox(msg, _T("Enable Encryption"), MB_ICONERROR);
	}
	catch (...)
	{
		m_csEncryptionStatus = _T("Unknown error enabling encryption.");
		MessageBox(_T("An unknown error occurred."), _T("Enable Encryption"), MB_ICONERROR);
	}

	UpdateData(FALSE);
}

// ---------------------------------------------------------------------------
// OnBtnTestEncryption
// ---------------------------------------------------------------------------
void COptionCloud::OnBtnTestEncryption()
{
	try
	{
		// Encrypt and decrypt a test string
		CStringA testPlain = "Hello Ditto Cloud";
		CStringA encrypted = CCloudCrypto::Encrypt(testPlain);
		if (encrypted.IsEmpty())
		{
			m_csEncryptionStatus = _T("加密测试失败: encryption failed.");
			MessageBox(_T("加密测试失败。Encryption is not working correctly."),
			           _T("Test Encryption"), MB_ICONERROR);
			return;
		}

		CStringA decrypted = CCloudCrypto::Decrypt(encrypted);
		if (decrypted == testPlain)
		{
			m_csEncryptionStatus = _T("加密测试成功");
			MessageBox(_T("加密测试成功！\nEncryption is working correctly."),
			           _T("Test Encryption"), MB_ICONINFORMATION);
		}
		else
		{
			m_csEncryptionStatus = _T("加密测试失败: decrypted text does not match.");
			MessageBox(_T("加密测试失败。Decrypted text does not match original."),
			           _T("Test Encryption"), MB_ICONERROR);
		}
	}
	catch (const std::exception& e)
	{
		m_csEncryptionStatus.Format(_T("加密测试失败: %hs"), e.what());
		CString msg;
		msg.Format(_T("Test encryption error: %hs"), e.what());
		MessageBox(msg, _T("Test Encryption"), MB_ICONERROR);
	}
	catch (...)
	{
		m_csEncryptionStatus = _T("加密测试失败: unknown error.");
		MessageBox(_T("An unknown error occurred during encryption test."),
		           _T("Test Encryption"), MB_ICONERROR);
	}

	UpdateData(FALSE);
}
