#include "stdafx.h"
#include "OptionCloud.h"
#include "CloudAuth.h"
#include "CloudCrypto.h"
#include "CloudKeyExport.h"
#include "../httplib.h"
#include "../json.hpp"
#include "../Options.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// ---------------------------------------------------------------------------
// CInputBox - simple input dialog helper (inline)
// Must be defined before COptionCloud methods that use it (OnBtnRegister, etc.)
// ---------------------------------------------------------------------------
class CInputBox : public CDialog
{
public:
	CString m_csTitle;
	CString m_csPrompt;
	CString m_csInput;

	CInputBox() : CDialog((LPCTSTR)nullptr) {}

protected:
	CStatic m_wndLabel;
	CEdit m_wndEdit;
	CButton m_wndOk;
	CButton m_wndCancel;

	virtual BOOL OnInitDialog()
	{
		CDialog::OnInitDialog();
		SetWindowText(m_csTitle);

		CRect rcClient;
		GetClientRect(&rcClient);

		int yPos = 20;
		m_wndLabel.Create(m_csPrompt, WS_CHILD | WS_VISIBLE | SS_LEFT,
			CRect(15, yPos, rcClient.right - 15, yPos + 40), this, 1001);
		yPos += 45;

		m_wndEdit.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
			CRect(15, yPos, rcClient.right - 15, yPos + 22), this, 1002);
		m_wndEdit.SetWindowText(m_csInput);
		yPos += 30;

		m_wndOk.Create(_T("确定"), WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
			CRect(rcClient.right / 2 - 80, yPos, rcClient.right / 2 - 10, yPos + 25), this, IDOK);

		m_wndCancel.Create(_T("取消"), WS_CHILD | WS_VISIBLE,
			CRect(rcClient.right / 2 + 10, yPos, rcClient.right / 2 + 70, yPos + 25), this, IDCANCEL);

		return TRUE;
	}

	virtual void OnOK()
	{
		m_wndEdit.GetWindowText(m_csInput);
		if (m_csInput.IsEmpty())
		{
			MessageBox(_T("请输入内容。"), _T("提示"), MB_ICONWARNING);
			return;
		}
		CDialog::OnOK();
	}
};

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
	ON_BN_CLICKED(IDC_CLOUD_BTN_EXPORT_KEY, &COptionCloud::OnBtnExportKey)
	ON_BN_CLICKED(IDC_CLOUD_BTN_IMPORT_KEY, &COptionCloud::OnBtnImportKey)
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

	// Prompt for email address (required for registration)
	CInputBox emailDlg;
	emailDlg.m_csTitle = _T("注册 - 输入邮箱");
	emailDlg.m_csPrompt = _T("请输入您的邮箱地址：");
	emailDlg.m_csInput = m_csUsername + _T("@");  // Pre-fill hint
	if (emailDlg.DoModal() != IDOK || emailDlg.m_csInput.IsEmpty())
	{
		m_csStatus = _T("Registration cancelled.");
		UpdateData(FALSE);
		return;
	}
	CString email = emailDlg.m_csInput;

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

// ---------------------------------------------------------------------------
// OnBtnExportKey: export encryption key to .dittokey file
// ---------------------------------------------------------------------------
void COptionCloud::OnBtnExportKey()
{
	// Verify encryption key exists
	CString csKeyB64 = CGetSetOptions::GetCloudEncryptionKey();
	if (csKeyB64.IsEmpty())
	{
		MessageBox(_T("请先启用加密（设置端到端加密密码），然后再导出密钥文件。"),
		           _T("导出密钥"), MB_ICONWARNING);
		return;
	}

	// Prompt for password to encrypt the exported key
	CInputBox dlg;
	dlg.m_csTitle = _T("导出密钥文件");
	dlg.m_csPrompt = _T("请输入密码以保护密钥文件：\n（忘记此密码将无法导入）");
	dlg.m_csInput = _T("");
	if (dlg.DoModal() != IDOK || dlg.m_csInput.IsEmpty())
		return;

	CString exportPassword = dlg.m_csInput;

	// Choose save location
	CFileDialog dlgFile(FALSE, _T("dittokey"), _T("ditto-cloud.dittokey"),
		OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT,
		_T("Ditto Key Files (*.dittokey)|*.dittokey|All Files (*.*)|*.*||"), this);

	if (dlgFile.DoModal() != IDOK)
		return;

	CString filePath = dlgFile.GetPathName();

	// Get username
	CString username = m_csUsername;
	if (username.IsEmpty())
	{
		TCHAR szComputerName[MAX_COMPUTERNAME_LENGTH + 1];
		DWORD dwSize = ARRAYSIZE(szComputerName);
		if (GetComputerName(szComputerName, &dwSize))
			username = szComputerName;
		else
			username = _T("unknown");
	}

	if (CCloudKeyExport::ExportKey(filePath, username, exportPassword))
	{
		CString msg;
		msg.Format(_T("密钥文件已成功导出到：\n%s\n\n请妥善保管此文件，切勿与其他人共享。"), filePath);
		MessageBox(msg, _T("导出密钥"), MB_ICONINFORMATION);
		m_csKeyFilePath = filePath;
	}
	else
	{
		MessageBox(_T("导出密钥文件失败。请检查文件路径和权限。"),
		           _T("导出密钥"), MB_ICONERROR);
	}

	UpdateData(FALSE);
}

// ---------------------------------------------------------------------------
// OnBtnImportKey: import encryption key from .dittokey file
// ---------------------------------------------------------------------------
void COptionCloud::OnBtnImportKey()
{
	// Choose key file
	CFileDialog dlgFile(TRUE, _T("dittokey"), nullptr,
		OFN_HIDEREADONLY | OFN_FILEMUSTEXIST,
		_T("Ditto Key Files (*.dittokey)|*.dittokey|All Files (*.*)|*.*||"), this);

	if (dlgFile.DoModal() != IDOK)
		return;

	CString filePath = dlgFile.GetPathName();

	// Validate key file
	if (!CCloudKeyExport::IsValidKeyFile(filePath))
	{
		MessageBox(_T("无效的密钥文件格式。"), _T("导入密钥"), MB_ICONERROR);
		return;
	}

	// Get key file info
	DittoKeyData keyInfo;
	if (!CCloudKeyExport::GetKeyFileInfo(filePath, keyInfo))
	{
		MessageBox(_T("无法读取密钥文件信息。"), _T("导入密钥"), MB_ICONERROR);
		return;
	}

	// Show key file info
	CString info;
	info.Format(_T("密钥文件信息：\n  用户名：%s\n  创建时间：%s\n  版本：%d\n\n请输入密码以解密密钥："),
		keyInfo.username, keyInfo.createdAt, keyInfo.version);
	MessageBox(info, _T("导入密钥"), MB_ICONINFORMATION);

	// Prompt for password
	CInputBox dlg;
	dlg.m_csTitle = _T("导入密钥文件");
	dlg.m_csPrompt = _T("请输入导出密钥文件时使用的密码：");
	dlg.m_csInput = _T("");
	if (dlg.DoModal() != IDOK || dlg.m_csInput.IsEmpty())
		return;

	CString importPassword = dlg.m_csInput;

	// Import
	DittoKeyData importedKey;
	if (CCloudKeyExport::ImportKey(filePath, importPassword, importedKey))
	{
		CString msg;
		msg.Format(_T("密钥文件导入成功！\n用户名：%s\n创建时间：%s\n\n加密已自动启用。"),
			importedKey.username, importedKey.createdAt);
		MessageBox(msg, _T("导入密钥"), MB_ICONINFORMATION);

		m_bEncryptionEnabled = TRUE;
		m_csEncryptionStatus = _T("Encryption enabled (imported from key file).");
		m_csKeyFilePath = filePath;
	}
	else
	{
		MessageBox(_T("导入密钥文件失败。密码可能不正确。"),
		           _T("导入密钥"), MB_ICONERROR);
	}

	UpdateData(FALSE);
}
