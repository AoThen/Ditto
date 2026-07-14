#include "stdafx.h"
#include "OptionCloud.h"
#include "CloudAuth.h"
#include "CloudCrypto.h"
#include "CloudKeyExport.h"
#include "CloudSyncManager.h"
#include "CloudEncryption.h"
#include "../httplib.h"
#include "../json.hpp"
#include "../Options.h"
#include "../CP_Main.h"

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
	DDX_Text(pDX, IDC_CLOUD_ENCRYPTION_PASSWORD, m_csEncryptionPassword);
	DDX_Text(pDX, IDC_CLOUD_ENCRYPTION_STATUS, m_csEncryptionStatus);
	DDX_Check(pDX, IDC_CLOUD_BTN_ENABLE_ENCRYPTION, m_bEncryptionEnabled);
	DDX_Text(pDX, IDC_CLOUD_KEY_FILE_PATH, m_csKeyFilePath);
}

BEGIN_MESSAGE_MAP(COptionCloud, CPropertyPage)
	ON_BN_CLICKED(IDC_CLOUD_BTN_LOGIN, &COptionCloud::OnBtnLogin)
	ON_BN_CLICKED(IDC_CLOUD_BTN_REGISTER, &COptionCloud::OnBtnRegister)
	ON_BN_CLICKED(IDC_CLOUD_BTN_ENABLE_ENCRYPTION, &COptionCloud::OnBtnEnableEncryption)
	ON_BN_CLICKED(IDC_CLOUD_BTN_TEST_ENCRYPTION, &COptionCloud::OnBtnTestEncryption)
	ON_BN_CLICKED(IDC_CLOUD_BTN_EXPORT_KEY, &COptionCloud::OnBtnExportKey)
	ON_BN_CLICKED(IDC_CLOUD_BTN_IMPORT_KEY, &COptionCloud::OnBtnImportKey)
	ON_MESSAGE(WM_CLOUD_AUTH_REQUIRED, &COptionCloud::OnCloudAuthRequired)
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

	// Load username from encryption salt (if available) or show last used
	CStringA token = CGetSetOptions::GetCloudDeviceToken();
	if (!token.IsEmpty())
	{
		// Extract device ID to show more status info
		CStringA deviceId = CGetSetOptions::GetCloudDeviceId();
		if (!deviceId.IsEmpty())
		{
			m_csStatus.Format(_T("已登录 (设备: %hs)"), deviceId.GetString());
		}
		else
		{
			m_csStatus = _T("已登录");
		}
	}
	else
	{
		m_csStatus = _T("未登录");
	}

	// Load encryption password status
	CString csKeyB64 = CGetSetOptions::GetCloudEncryptionKey();
	CString csSalt = CGetSetOptions::GetCloudEncryptionSalt();
	if (!csKeyB64.IsEmpty())
	{
		m_bEncryptionEnabled = TRUE;
		if (!csSalt.IsEmpty())
		{
			// Show salt info (first 16 chars for security)
			CString saltPreview = csSalt.Left(16);
			m_csEncryptionStatus.Format(_T("加密已启用 (Salt: %s...)"), saltPreview);
		}
		else
		{
			m_csEncryptionStatus = _T("加密已启用");
		}
	}
	else
	{
		m_csEncryptionStatus = _T("未启用加密");
	}

	// Load key file path if previously exported
	m_csKeyFilePath = CGetSetOptions::GetCloudKeyFilePath();
	if (!m_csKeyFilePath.IsEmpty())
	{
		// Verify file still exists
		if (GetFileAttributes(m_csKeyFilePath) == INVALID_FILE_ATTRIBUTES)
		{
			m_csKeyFilePath = _T("");
		}
	}

	UpdateData(FALSE);

	return TRUE;  // return TRUE unless you set the focus to a control
}

BOOL COptionCloud::OnApply()
{
	// Validate server URL if cloud sync is enabled
	if (m_bEnabled && m_csServerUrl.IsEmpty())
	{
		MessageBox(_T("启用云端同步前，请输入服务器地址。"), _T("配置错误"), MB_ICONWARNING);
		return FALSE;
	}

	// Save values via CGetSetOptions
	CGetSetOptions::SetCloudSyncEnabled(m_bEnabled);
	CGetSetOptions::SetCloudAutoSync(m_bAutoSync);
	CGetSetOptions::SetCloudServerUrl(m_csServerUrl);
	CGetSetOptions::SetCloudLastUsername(m_csUsername);
	if (!m_csKeyFilePath.IsEmpty())
	{
		CGetSetOptions::SetCloudKeyFilePath(m_csKeyFilePath);
	}

	// Re-initialize sync if settings changed
	if (m_bEnabled && theApp.m_pCloudSyncManager != nullptr)
	{
		// Reinitialize with new settings
		theApp.m_pCloudSyncManager->Initialize();
	}

	return CPropertyPage::OnApply();
}

void COptionCloud::OnBtnLogin()
{
	// Validate fields
	if (m_csServerUrl.IsEmpty())
	{
		MessageBox(_T("请输入服务器地址。"), _T("登录"), MB_ICONWARNING);
		return;
	}

	if (m_csUsername.IsEmpty())
	{
		MessageBox(_T("请输入用户名。"), _T("登录"), MB_ICONWARNING);
		return;
	}

	if (m_csPassword.IsEmpty())
	{
		MessageBox(_T("请输入密码。"), _T("登录"), MB_ICONWARNING);
		return;
	}

	// Save username for convenience
	CGetSetOptions::SetCloudLastUsername(m_csUsername);

	// Show status
	m_csStatus = _T("正在登录...");
	UpdateData(FALSE);

	try
	{
		LoginResult result = CCloudAuth::Login(m_csServerUrl, m_csUsername, m_csPassword);

		if (result.success)
		{
			m_csStatus.Format(_T("已登录 (设备: %s)"), result.deviceId.IsEmpty() ? _T("未知") : result.deviceId.GetString());
			MessageBox(_T("登录成功！\n现在可以启用云端同步。"), _T("登录"), MB_ICONINFORMATION);
			
			// Update encryption status if key exists
			CString csKeyB64 = CGetSetOptions::GetCloudEncryptionKey();
			if (!csKeyB64.IsEmpty())
			{
				m_csEncryptionStatus = _T("加密已启用");
			}
		}
		else
		{
			m_csStatus = result.error;
			CString msg;
			msg.Format(_T("登录失败: %s"), result.error.GetString());
			MessageBox(msg, _T("登录"), MB_ICONERROR);
		}
	}
	catch (const std::exception& e)
	{
		m_csStatus.Format(_T("异常: %hs"), e.what());
		CString msg;
		msg.Format(_T("登录错误: %hs"), e.what());
		MessageBox(msg, _T("登录"), MB_ICONERROR);
	}
	catch (...)
	{
		m_csStatus = _T("登录时发生未知错误。");
		MessageBox(_T("登录时发生未知错误。"), _T("登录"), MB_ICONERROR);
	}

	UpdateData(FALSE);
}

void COptionCloud::OnBtnRegister()
{
	MessageBox(_T("注册功能已关闭，请联系管理员创建账号。"), _T("注册"), MB_ICONINFORMATION);
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

	m_csEncryptionStatus = _T("正在设置加密...");
	UpdateData(FALSE);

	try
	{
		CStringA token = CGetSetOptions::GetCloudDeviceToken();
		if (token.IsEmpty())
		{
			m_csEncryptionStatus = _T("请先登录后再启用加密。");
			MessageBox(m_csEncryptionStatus, _T("Enable Encryption"), MB_ICONERROR);
			return;
		}

		EncryptionSetupResult result = CCloudEncryption::SetupEncryption(
			m_csServerUrl, CString(token), m_csEncryptionPassword);

		if (result.success)
		{
			m_bEncryptionEnabled = TRUE;
			m_csEncryptionStatus.Format(_T("加密已启用 (Salt: %s...)"), result.salt.Left(16));
			MessageBox(_T("加密已启用。您的数据将在同步前进行加密。"),
			           _T("Enable Encryption"), MB_ICONINFORMATION);
		}
		else
		{
			m_csEncryptionStatus = result.error;
			MessageBox(result.error, _T("Enable Encryption"), MB_ICONERROR);
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
		msg.Format(_T("密钥文件已成功导出到：\n%s\n\n⚠️ 重要提示：\n• 请妥善保管此文件，切勿与其他人共享\n• 忘记密码或密钥文件将导致数据不可恢复"), filePath);
		MessageBox(msg, _T("导出密钥"), MB_ICONINFORMATION);
		m_csKeyFilePath = filePath;
		CGetSetOptions::SetCloudKeyFilePath(filePath);
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
		m_csEncryptionStatus.Format(_T("加密已启用 (从密钥文件导入: %s)"), importedKey.username);
		m_csKeyFilePath = filePath;
		CGetSetOptions::SetCloudKeyFilePath(filePath);
	}
	else
	{
		MessageBox(_T("导入密钥文件失败。密码可能不正确。"),
		           _T("导入密钥"), MB_ICONERROR);
	}

	UpdateData(FALSE);
}

// ---------------------------------------------------------------------------
// OnCloudAuthRequired: Handler for WM_CLOUD_AUTH_REQUIRED message
// Called when sync thread detects token expiration (401/403) or encryption issue (999)
// ---------------------------------------------------------------------------
LRESULT COptionCloud::OnCloudAuthRequired(WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	
	UINT statusCode = static_cast<UINT>(wParam);
	CString msg;
	
	if (statusCode == 401)
	{
		msg = _T("云端认证令牌已过期。\n\n")
		      _T("您的同步已暂停，请重新登录以继续同步。\n\n")
		      _T("点击\"确定\"后，将打开登录对话框。");
		
		MessageBox(msg, _T("云端同步 - 需要重新认证"), MB_ICONWARNING | MB_OK);
		
		// Automatically open login dialog flow
		OnBtnLogin();
	}
	else if (statusCode == 403)
	{
		msg = _T("云端访问被拒绝（HTTP 403）。\n\n")
		      _T("可能原因：\n")
		      _T("• 您的账号已被禁用\n")
		      _T("• 设备已被管理员移除\n\n")
		      _T("请联系管理员或重新登录。");
		
		MessageBox(msg, _T("云端同步 - 访问被拒绝"), MB_ICONERROR | MB_OK);
		
		// Clear credentials and prompt for re-login
		CCloudAuth::Logout();
		OnBtnLogin();
	}
	else if (statusCode == 998)
	{
		// Encryption password changed on another device
		msg = _T("加密密码已变更！\n\n")
		      _T("您的加密密码已在其他设备上修改。\n")
		      _T("请重新输入新的加密密码以继续同步。\n\n")
		      _T("点击\"确定\"后将打开加密设置。");
		
		MessageBox(msg, _T("云端同步 - 加密密码已变更"), MB_ICONWARNING | MB_OK);
		
		// Open this property page to show encryption settings
		CPropertySheet* pSheet = static_cast<CPropertySheet*>(GetParent());
		if (pSheet != nullptr)
		{
			pSheet->SetActivePage(this);
		}
	}
	else if (statusCode == 999)
	{
		// Encryption initialization failed
		msg = _T("加密初始化失败！\n\n")
		      _T("⚠️ 警告：您的剪贴板数据将不会被加密。\n\n")
		      _T("可能原因：\n")
		      _T("• 未设置加密密码\n")
		      _T("• 未导入密钥文件\n")
		      _T("• 加密服务不可用\n\n")
		      _T("请在\"云端同步\"设置中重新启用加密，\n")
		      _T("以保护您的隐私数据。");
		
		MessageBox(msg, _T("云端同步 - 加密失败"), MB_ICONWARNING | MB_OK);
		
		// Open this property page to show encryption settings
		CPropertySheet* pSheet = static_cast<CPropertySheet*>(GetParent());
		if (pSheet != nullptr)
		{
			pSheet->SetActivePage(this);
		}
	}
	
	return 0;
}
