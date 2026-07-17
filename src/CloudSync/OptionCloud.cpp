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
#include "../Pinyin_Convert.h"

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

		m_wndEdit.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_PASSWORD,
			CRect(15, yPos, rcClient.right - 15, yPos + 22), this, 1002);
		m_wndEdit.SetWindowText(m_csInput);
		yPos += 30;

		m_wndOk.Create(theApp.m_Language.GetString("CloudBtnOK", "OK"), WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
			CRect(rcClient.right / 2 - 80, yPos, rcClient.right / 2 - 10, yPos + 25), this, IDOK);

		m_wndCancel.Create(theApp.m_Language.GetString("CloudBtnCancel", "Cancel"), WS_CHILD | WS_VISIBLE,
			CRect(rcClient.right / 2 + 10, yPos, rcClient.right / 2 + 70, yPos + 25), this, IDCANCEL);

		return TRUE;
	}

	virtual void OnOK()
	{
		m_wndEdit.GetWindowText(m_csInput);
		if (m_csInput.IsEmpty())
		{
			MessageBox(theApp.m_Language.GetString("CloudMsgInputRequired", "Please enter a value."),
			theApp.m_Language.GetString("CloudTitlePrompt", "Prompt"), MB_ICONWARNING);
			return;
		}
		CDialog::OnOK();
	}
};

IMPLEMENT_DYNCREATE(COptionCloud, CPropertyPage)

COptionCloud::COptionCloud()
	: CPropertyPage(COptionCloud::IDD)
	, m_bEnabled(FALSE)
	, m_bPushOnCopy(TRUE)
	, m_bPeriodicSync(TRUE)
	, m_csSyncInterval(_T("30"))
	, m_csSyncStatus(_T(""))
	, m_csServerUrl(CLOUD_DEFAULT_SERVER_URL)
	, m_csUsername(_T(""))
	, m_csPassword(_T(""))
	, m_csStatus(_T(""))
	, m_csEncryptionPassword(_T(""))
	, m_csEncryptionStatus(_T(""))
	, m_bEncryptionEnabled(FALSE)
	, m_nStatusTimer(0)
{
	m_csTitle = theApp.m_Language.GetString("CloudSyncTitle", "Cloud Sync");
	m_psp.pszTitle = m_csTitle;
	m_psp.dwFlags |= PSP_USETITLE;
}

COptionCloud::~COptionCloud()
{
}

void COptionCloud::DoDataExchange(CDataExchange* pDX)
{
	CPropertyPage::DoDataExchange(pDX);
	DDX_Check(pDX, IDC_CLOUD_ENABLE, m_bEnabled);
	DDX_Check(pDX, IDC_CLOUD_PUSH_ON_COPY, m_bPushOnCopy);
	DDX_Check(pDX, IDC_CLOUD_PERIODIC_SYNC, m_bPeriodicSync);
	DDX_Text(pDX, IDC_CLOUD_SYNC_INTERVAL, m_csSyncInterval);
	DDX_Text(pDX, IDC_CLOUD_SYNC_STATUS, m_csSyncStatus);
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
	ON_WM_TIMER()
	ON_BN_CLICKED(IDC_CLOUD_BTN_LOGIN, &COptionCloud::OnBtnLogin)
	ON_BN_CLICKED(IDC_CLOUD_BTN_REGISTER, &COptionCloud::OnBtnRegister)
	ON_BN_CLICKED(IDC_CLOUD_BTN_ENABLE_ENCRYPTION, &COptionCloud::OnBtnEnableEncryption)
	ON_BN_CLICKED(IDC_CLOUD_BTN_TEST_ENCRYPTION, &COptionCloud::OnBtnTestEncryption)
	ON_BN_CLICKED(IDC_CLOUD_BTN_EXPORT_KEY, &COptionCloud::OnBtnExportKey)
	ON_BN_CLICKED(IDC_CLOUD_BTN_IMPORT_KEY, &COptionCloud::OnBtnImportKey)
	ON_BN_CLICKED(IDC_CLOUD_FORCE_DOWNLOAD, &COptionCloud::OnBtnForceDownload)
	ON_BN_CLICKED(IDC_CLOUD_FORCE_UPLOAD, &COptionCloud::OnBtnForceUpload)
	ON_BN_CLICKED(IDC_REBUILD_PINYIN, &COptionCloud::OnRebuildPinyinIndex)
	ON_MESSAGE(WM_CLOUD_AUTH_REQUIRED, &COptionCloud::OnCloudAuthRequired)
	ON_MESSAGE(WM_CLOUD_REINIT_SYNC, &COptionCloud::OnReinitSync)
END_MESSAGE_MAP()

BOOL COptionCloud::OnInitDialog()
{
	CPropertyPage::OnInitDialog();

	theApp.m_Language.UpdateOptionCloud(this);

	// Load saved values from CGetSetOptions
	m_bEnabled = CGetSetOptions::GetCloudSyncEnabled();
	m_bPushOnCopy = CGetSetOptions::GetCloudPushOnCopy();
	m_bPeriodicSync = CGetSetOptions::GetCloudPeriodicSync();
	m_csSyncInterval.Format(_T("%d"), CGetSetOptions::GetCloudSyncInterval());
	m_nStatusTimer = 0;
	m_csServerUrl = CGetSetOptions::GetCloudServerUrl();
	if (m_csServerUrl.IsEmpty())
	{
		m_csServerUrl = CLOUD_DEFAULT_SERVER_URL;
	}

	// Load username from encryption salt (if available) or show last used
	CStringA token = CGetSetOptions::GetCloudDeviceToken();
	if (!token.IsEmpty())
	{
		// Extract device ID to show more status info
		CStringA deviceId = CGetSetOptions::GetCloudDeviceId();
		if (!deviceId.IsEmpty())
		{
			m_csStatus.Format(theApp.m_Language.GetString("CloudStatusLoggedInDevice", "Logged in (Device: %hs)"), deviceId.GetString());
		}
		else
		{
			m_csStatus = theApp.m_Language.GetString("CloudStatusLoggedIn", "Logged in");
		}
	}
	else
	{
		m_csStatus = theApp.m_Language.GetString("CloudStatusNotLoggedIn", "Not logged in");
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
			m_csEncryptionStatus.Format(theApp.m_Language.GetString("CloudEncryptionEnabledSalt", "Encryption enabled (Salt: %s...)"), saltPreview);
		}
		else
		{
			m_csEncryptionStatus = theApp.m_Language.GetString("CloudEncryptionEnabled", "Encryption enabled");
		}
	}
	else
	{
		m_csEncryptionStatus = theApp.m_Language.GetString("CloudEncryptionNotEnabled", "Encryption not enabled");
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

	// Check if encryption needs recovery (DEK lost at startup)
	// This handles the case where WM_CLOUD_AUTH_REQUIRED(997) was posted
	// before this property page was created and thus never delivered.
	if (CGetSetOptions::GetCloudEncryptionNeedsRecovery() && m_bEnabled)
	{
		PostMessage(WM_CLOUD_AUTH_REQUIRED, 997, 0);
	}

	RefreshSyncStatus();

	return TRUE;  // return TRUE unless you set the focus to a control
}

BOOL COptionCloud::OnApply()
{
	// Validate server URL if cloud sync is enabled
	if (m_bEnabled && m_csServerUrl.IsEmpty())
	{
		MessageBox(theApp.m_Language.GetString("CloudMsgEnterServerUrl", "Please enter the server URL before enabling cloud sync."),
		theApp.m_Language.GetString("CloudTitleConfigError", "Configuration Error"), MB_ICONWARNING);
		return FALSE;
	}

	// Save values via CGetSetOptions
	CGetSetOptions::SetCloudSyncEnabled(m_bEnabled);
	CGetSetOptions::SetCloudPushOnCopy(m_bPushOnCopy);
	CGetSetOptions::SetCloudPeriodicSync(m_bPeriodicSync);
	int nInterval = _ttoi(m_csSyncInterval);
	if (nInterval < 5) nInterval = 5;
	if (nInterval > 300) nInterval = 300;
	CGetSetOptions::SetCloudSyncInterval(nInterval);
	m_csSyncInterval.Format(_T("%d"), nInterval);
	CGetSetOptions::SetCloudServerUrl(m_csServerUrl);
	CGetSetOptions::SetCloudLastUsername(m_csUsername);
	if (!m_csKeyFilePath.IsEmpty())
	{
		CGetSetOptions::SetCloudKeyFilePath(m_csKeyFilePath);
	}

	// Re-initialize sync if settings changed
	if (m_bEnabled)
	{
		// ReinitializeSync handles Stop + Initialize (safe when already running)
		theApp.m_CloudSyncManager.ReinitializeSync();
	}
	else
	{
		theApp.m_CloudSyncManager.Stop();
	}

	return CPropertyPage::OnApply();
}

BOOL COptionCloud::OnSetActive()
{
	BOOL bResult = CPropertyPage::OnSetActive();
	if (bResult)
	{
		m_nStatusTimer = SetTimer(1, 5000, NULL);
		RefreshSyncStatus();
	}
	return bResult;
}

BOOL COptionCloud::OnKillActive()
{
	if (m_nStatusTimer)
	{
		KillTimer(m_nStatusTimer);
		m_nStatusTimer = 0;
	}
	return CPropertyPage::OnKillActive();
}

void COptionCloud::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == 1)
		RefreshSyncStatus();
	CPropertyPage::OnTimer(nIDEvent);
}

void COptionCloud::RefreshSyncStatus()
{
	CCloudSyncManager& mgr = theApp.m_CloudSyncManager;
	CString csNew;

	if (!m_bEnabled || CGetSetOptions::GetCloudDeviceToken().IsEmpty())
	{
		csNew = _T("");
	}
	else if (mgr.GetSyncStatus() == _T("Syncing..."))
	{
		csNew = theApp.m_Language.GetString("CloudStatusSyncing", _T("Syncing..."));
	}
	else if (mgr.GetSyncStatus() == _T("Error"))
	{
		CString err = mgr.GetLastError();
		if (err.IsEmpty())
			err = _T("Unknown error");
		csNew.Format(theApp.m_Language.GetString("CloudStatusError", _T("Error: %s")), err);
	}
	else if (mgr.HasSyncedBefore())
	{
		CTime t(mgr.GetLastSyncSuccessTime());
		csNew.Format(theApp.m_Language.GetString("CloudStatusLastSync", _T("Last sync: %s \u2713")),
			t.Format(_T("%H:%M:%S")));
	}
	else
	{
		csNew = theApp.m_Language.GetString("CloudStatusNeverSynced", _T("Never synced"));
	}

	if (csNew != m_csSyncStatus)
	{
		m_csSyncStatus = csNew;
		UpdateData(FALSE);
	}
}

void COptionCloud::OnBtnLogin()
{
	// Validate fields
	if (m_csServerUrl.IsEmpty())
	{
		MessageBox(theApp.m_Language.GetString("CloudMsgEnterServerUrlLogin", "Please enter the server URL."),
			theApp.m_Language.GetString("CloudTitleLogin", "Login"), MB_ICONWARNING);
		return;
	}

	if (m_csUsername.IsEmpty())
	{
		MessageBox(theApp.m_Language.GetString("CloudMsgEnterUsername", "Please enter a username."),
			theApp.m_Language.GetString("CloudTitleLogin", "Login"), MB_ICONWARNING);
		return;
	}

	if (m_csPassword.IsEmpty())
	{
		MessageBox(theApp.m_Language.GetString("CloudMsgEnterPassword", "Please enter a password."),
			theApp.m_Language.GetString("CloudTitleLogin", "Login"), MB_ICONWARNING);
		return;
	}

	// Save username for convenience
	CGetSetOptions::SetCloudLastUsername(m_csUsername);

	// Show status
	m_csStatus = theApp.m_Language.GetString("CloudStatusLoggingIn", "Logging in...");
	UpdateData(FALSE);

	try
	{
		LoginResult result = CCloudAuth::Login(m_csServerUrl, m_csUsername, m_csPassword);

		if (result.success)
		{
			m_csStatus.Format(theApp.m_Language.GetString("CloudStatusLoggedInDevice2", "Logged in (Device: %s)"), result.deviceId.IsEmpty() ? theApp.m_Language.GetString("CloudUnknown", "Unknown") : result.deviceId.GetString());
			MessageBox(theApp.m_Language.GetString("CloudMsgLoginSuccess", "Login successful!\nYou can now enable cloud sync."),
			           theApp.m_Language.GetString("CloudTitleLogin", "Login"), MB_ICONINFORMATION);
			
			// Update encryption status if key exists
			CString csKeyB64 = CGetSetOptions::GetCloudEncryptionKey();
			if (!csKeyB64.IsEmpty())
			{
m_csEncryptionStatus = theApp.m_Language.GetString("CloudEncryptionEnabled", "Encryption enabled");
			}
		}
		else
		{
			m_csStatus = result.error;
			CString msg;
			msg.Format(theApp.m_Language.GetString("CloudMsgLoginFailed", "Login failed: %s"), result.error.GetString());
			MessageBox(msg, theApp.m_Language.GetString("CloudTitleLogin", "Login"), MB_ICONERROR);
		}
	}
	catch (const std::exception& e)
	{
		m_csStatus.Format(theApp.m_Language.GetString("CloudStatusException", "Exception: %hs"), e.what());
		CString msg;
		msg.Format(theApp.m_Language.GetString("CloudMsgLoginError", "Login error: %hs"), e.what());
		MessageBox(msg, theApp.m_Language.GetString("CloudTitleLogin", "Login"), MB_ICONERROR);
	}
	catch (...)
	{
		m_csStatus = theApp.m_Language.GetString("CloudStatusLoginUnknownError", "An unknown error occurred during login.");
		MessageBox(theApp.m_Language.GetString("CloudMsgLoginUnknownError", "An unknown error occurred during login."),
		           theApp.m_Language.GetString("CloudTitleLogin", "Login"), MB_ICONERROR);
	}

	UpdateData(FALSE);
}

void COptionCloud::OnBtnRegister()
{
	MessageBox(theApp.m_Language.GetString("CloudMsgRegisterDisabled", "Registration is closed. Please contact your administrator to create an account."),
		theApp.m_Language.GetString("CloudTitleRegister", "Register"), MB_ICONINFORMATION);
	UpdateData(FALSE);
}

// ---------------------------------------------------------------------------
// OnBtnEnableEncryption
// ---------------------------------------------------------------------------
void COptionCloud::OnBtnEnableEncryption()
{
	if (m_csEncryptionPassword.IsEmpty())
	{
		MessageBox(theApp.m_Language.GetString("CloudMsgEnterEncryptionPassword", "Please enter an encryption password."), theApp.m_Language.GetString("CloudTitleEnableEncryption", "Enable Encryption"), MB_ICONWARNING);
		return;
	}

	m_csEncryptionStatus = theApp.m_Language.GetString("CloudStatusSettingUpEncryption", "Setting up encryption...");
	UpdateData(FALSE);

	try
	{
		CStringA token = CGetSetOptions::GetCloudDeviceToken();
		if (token.IsEmpty())
		{
			m_csEncryptionStatus = theApp.m_Language.GetString("CloudStatusLoginRequired", "Please log in first before enabling encryption.");
			MessageBox(m_csEncryptionStatus, theApp.m_Language.GetString("CloudTitleEnableEncryption", "Enable Encryption"), MB_ICONERROR);
			return;
		}

		EncryptionSetupResult result = CCloudEncryption::SetupEncryption(
			m_csServerUrl, CString(token), m_csEncryptionPassword);

		if (result.success)
		{
			m_bEncryptionEnabled = TRUE;
			m_csEncryptionStatus.Format(theApp.m_Language.GetString("CloudEncryptionEnabledSalt", "Encryption enabled (Salt: %s...)"), result.salt.Left(16));

			// Defer ReinitializeSync via PostMessage to avoid UI thread blocking
			// (Stop may wait up to 15s for sync thread). The handler runs after
			// the success message is dismissed and shows a warning on failure.
			PostMessage(WM_CLOUD_REINIT_SYNC, 0, 0);

			MessageBox(theApp.m_Language.GetString("CloudMsgEncryptionEnabled",
				"Encryption has been enabled. Your data will be encrypted before syncing."),
				theApp.m_Language.GetString("CloudTitleEnableEncryption", "Enable Encryption"), MB_ICONINFORMATION);
		}
		else
		{
			m_csEncryptionStatus = result.error;
			MessageBox(result.error, theApp.m_Language.GetString("CloudTitleEnableEncryption", "Enable Encryption"), MB_ICONERROR);
		}
	}
	catch (const std::exception& e)
	{
		m_csEncryptionStatus.Format(theApp.m_Language.GetString("CloudStatusEncryptionException", "Exception: %hs"), e.what());
		CString msg;
		msg.Format(theApp.m_Language.GetString("CloudMsgEncryptionError", "Error enabling encryption: %hs"), e.what());
		MessageBox(msg, theApp.m_Language.GetString("CloudTitleEnableEncryption", "Enable Encryption"), MB_ICONERROR);
	}
	catch (...)
	{
		m_csEncryptionStatus = theApp.m_Language.GetString("CloudStatusEncryptionUnknownError", "Unknown error enabling encryption.");
		MessageBox(theApp.m_Language.GetString("CloudMsgEncryptionUnknownError", "An unknown error occurred."),
		           theApp.m_Language.GetString("CloudTitleEnableEncryption", "Enable Encryption"), MB_ICONERROR);
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
			m_csEncryptionStatus = theApp.m_Language.GetString("CloudEncTestFailed", "Encryption test failed: encryption failed.");
			MessageBox(theApp.m_Language.GetString("CloudMsgEncTestFailed", "Encryption test failed. Encryption is not working correctly."),
			           theApp.m_Language.GetString("CloudTitleTestEncryption", "Test Encryption"), MB_ICONERROR);
			return;
		}

		CStringA decrypted = CCloudCrypto::Decrypt(encrypted);
		if (decrypted == testPlain)
		{
			m_csEncryptionStatus = theApp.m_Language.GetString("CloudEncTestSuccess", "Encryption test succeeded");
			MessageBox(theApp.m_Language.GetString("CloudMsgEncTestSuccess", "Encryption test succeeded!\nEncryption is working correctly."),
			           theApp.m_Language.GetString("CloudTitleTestEncryption", "Test Encryption"), MB_ICONINFORMATION);
		}
		else
		{
			m_csEncryptionStatus = theApp.m_Language.GetString("CloudEncTestMismatch", "Encryption test failed: decrypted text does not match.");
			MessageBox(theApp.m_Language.GetString("CloudMsgEncTestMismatch", "Encryption test failed. Decrypted text does not match original."),
			           theApp.m_Language.GetString("CloudTitleTestEncryption", "Test Encryption"), MB_ICONERROR);
		}
	}
	catch (const std::exception& e)
	{
		m_csEncryptionStatus.Format(theApp.m_Language.GetString("CloudEncTestException", "Encryption test failed: %hs"), e.what());
		CString msg;
		msg.Format(theApp.m_Language.GetString("CloudMsgEncTestError", "Test encryption error: %hs"), e.what());
		MessageBox(msg, theApp.m_Language.GetString("CloudTitleTestEncryption", "Test Encryption"), MB_ICONERROR);
	}
	catch (...)
	{
		m_csEncryptionStatus = theApp.m_Language.GetString("CloudEncTestUnknownError", "Encryption test failed: unknown error.");
		MessageBox(theApp.m_Language.GetString("CloudMsgEncTestUnknownError", "An unknown error occurred during encryption test."),
		           theApp.m_Language.GetString("CloudTitleTestEncryption", "Test Encryption"), MB_ICONERROR);
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
		MessageBox(theApp.m_Language.GetString("CloudMsgEnableEncryptionFirst", "Please enable encryption (set end-to-end encryption password) first, then export the key file."),
		           theApp.m_Language.GetString("CloudTitleExportKey", "Export Key"), MB_ICONWARNING);
		return;
	}

	// Prompt for password to encrypt the exported key
	CInputBox dlg;
	dlg.m_csTitle = theApp.m_Language.GetString("CloudTitleExportKey", "Export Key");
	dlg.m_csPrompt = theApp.m_Language.GetString("CloudPromptExportKeyPassword", "Enter a password to protect the key file:\n(Losing this password means the key cannot be imported)");
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
		msg.Format(theApp.m_Language.GetString("CloudMsgKeyExported",
			"Key file exported to:\n%s\n\nImportant:\n- Keep this file safe, do not share it\n- Losing the password or key file means data cannot be recovered"), filePath);
		MessageBox(msg, theApp.m_Language.GetString("CloudTitleExportKey", "Export Key"), MB_ICONINFORMATION);
		m_csKeyFilePath = filePath;
		CGetSetOptions::SetCloudKeyFilePath(filePath);
	}
	else
	{
		MessageBox(theApp.m_Language.GetString("CloudMsgExportFailed", "Failed to export key file. Please check the file path and permissions."),
		           theApp.m_Language.GetString("CloudTitleExportKey", "Export Key"), MB_ICONERROR);
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
		MessageBox(theApp.m_Language.GetString("CloudMsgInvalidKeyFile", "Invalid key file format."),
		           theApp.m_Language.GetString("CloudTitleImportKey", "Import Key"), MB_ICONERROR);
		return;
	}

	// Get key file info
	DittoKeyData keyInfo;
	if (!CCloudKeyExport::GetKeyFileInfo(filePath, keyInfo))
	{
		MessageBox(theApp.m_Language.GetString("CloudMsgCannotReadKeyFile", "Cannot read key file information."),
		           theApp.m_Language.GetString("CloudTitleImportKey", "Import Key"), MB_ICONERROR);
		return;
	}

	// Show key file info
	CString info;
	info.Format(theApp.m_Language.GetString("CloudMsgKeyFileInfo",
		"Key File Info:\n  Username: %s\n  Created: %s\n  Version: %d\n\nEnter password to decrypt the key:"),
		keyInfo.username, keyInfo.createdAt, keyInfo.version);
	MessageBox(info, theApp.m_Language.GetString("CloudTitleImportKey", "Import Key"), MB_ICONINFORMATION);

	// Prompt for password
	CInputBox dlg;
	dlg.m_csTitle = theApp.m_Language.GetString("CloudTitleImportKey", "Import Key");
	dlg.m_csPrompt = theApp.m_Language.GetString("CloudPromptImportKeyPassword", "Enter the password that was used when exporting the key file:");
	dlg.m_csInput = _T("");
	if (dlg.DoModal() != IDOK || dlg.m_csInput.IsEmpty())
		return;

	CString importPassword = dlg.m_csInput;

	// Import
	DittoKeyData importedKey;
	if (CCloudKeyExport::ImportKey(filePath, importPassword, importedKey))
	{
		CString msg;
		msg.Format(theApp.m_Language.GetString("CloudMsgKeyImported",
			"Key file imported successfully!\nUsername: %s\nCreated: %s\n\nEncryption has been enabled automatically."),
			importedKey.username, importedKey.createdAt);
		MessageBox(msg, theApp.m_Language.GetString("CloudTitleImportKey", "Import Key"), MB_ICONINFORMATION);

		m_bEncryptionEnabled = TRUE;
		m_csEncryptionStatus.Format(theApp.m_Language.GetString("CloudEncryptionEnabledImported", "Encryption enabled (imported from key: %s)"), importedKey.username);
		m_csKeyFilePath = filePath;
		CGetSetOptions::SetCloudKeyFilePath(filePath);
	}
	else
	{
		MessageBox(theApp.m_Language.GetString("CloudMsgImportFailed", "Failed to import key file. The password might be incorrect."),
		           theApp.m_Language.GetString("CloudTitleImportKey", "Import Key"), MB_ICONERROR);
	}

	UpdateData(FALSE);
}

// ---------------------------------------------------------------------------
// OnBtnForceDownload: one-shot force download from cloud, overwrite local
// ---------------------------------------------------------------------------
void COptionCloud::OnBtnForceDownload()
{
	theApp.m_CloudSyncManager.ForceDownloadAll();
	MessageBox(
		theApp.m_Language.GetString("CloudForceDownloadMsg",
			_T("Force download has been triggered.\nCloud clips will replace local clips.")),
		theApp.m_Language.GetString("CloudTitleForceSync", _T("Force Sync")),
		MB_ICONINFORMATION);
}

// ---------------------------------------------------------------------------
// OnBtnForceUpload: one-shot force upload to cloud, overwrite remote
// ---------------------------------------------------------------------------
void COptionCloud::OnBtnForceUpload()
{
	theApp.m_CloudSyncManager.ForceUploadAll();
	MessageBox(
		theApp.m_Language.GetString("CloudForceUploadMsg",
			_T("Force upload has been triggered.\nLocal clips are being pushed to the cloud.")),
		theApp.m_Language.GetString("CloudTitleForceSync", _T("Force Sync")),
		MB_ICONINFORMATION);
}

// ---------------------------------------------------------------------------
// OnRebuildPinyinIndex: rebuild pinyin search index for all clips
// ---------------------------------------------------------------------------
void COptionCloud::OnRebuildPinyinIndex()
{
	if (AfxMessageBox(_T("Rebuild pinyin search index for all clips?"), MB_YESNO) != IDYES)
		return;

	CPinyinConvert conv;
	CWaitCursor wait;

	CppSQLite3Query q = theApp.m_db.execQuery(
		_T("SELECT lID, mText FROM Main WHERE pinyin IS NULL OR pinyin = ''"));

	int batch = 0;
	theApp.m_db.execDML(_T("BEGIN TRANSACTION;"));
	while (!q.eof())
	{
		long id = q.getIntField(0);
		CString mText = q.getStringField(1);
		std::wstring wText(mText.GetString());
		std::string pinyin = conv.ConvertToPinyin(wText);
		std::string abbr = conv.ConvertToAbbreviation(wText);
		CString pinyinW = CA2T(pinyin.c_str(), CP_UTF8);
		CString abbrW = CA2T(abbr.c_str(), CP_UTF8);
		theApp.m_db.execDMLEx(
			_T("UPDATE Main SET pinyin = '%s', pinyinAbbr = '%s' WHERE lID = %d"),
			pinyinW, abbrW, id);

		if (++batch % 100 == 0) {
			theApp.m_db.execDML(_T("COMMIT; BEGIN TRANSACTION;"));
		}
		q.nextRow();
	}
	theApp.m_db.execDML(_T("COMMIT;"));

	CString msg;
	msg.Format(_T("Pinyin index rebuilt. %d entries processed."), batch);
	AfxMessageBox(msg);
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
msg = theApp.m_Language.GetString("CloudMsgAuthExpired",
			"Cloud authentication token has expired.\n\n"
			"Your sync has been paused. Please log in again to continue syncing.\n\n"
			"Click OK to open the login dialog.");

		MessageBox(msg, theApp.m_Language.GetString("CloudTitleAuthRequired", "Cloud Sync - Authentication Required"), MB_ICONWARNING | MB_OK);
		
		// Automatically open login dialog flow
		OnBtnLogin();
	}
	else if (statusCode == 403)
	{
msg = theApp.m_Language.GetString("CloudMsgAccessDenied",
			"Cloud access denied (HTTP 403).\n\n"
			"Possible causes:\n"
			"- Your account has been disabled\n"
			"- Your device has been removed by an administrator\n\n"
			"Please contact your administrator or log in again.");

		MessageBox(msg, theApp.m_Language.GetString("CloudTitleAccessDenied", "Cloud Sync - Access Denied"), MB_ICONERROR | MB_OK);
		
		// Clear credentials and prompt for re-login
		CCloudAuth::Logout();
		OnBtnLogin();
	}
	else if (statusCode == 998)
	{
		// Encryption password changed on another device — auto-prompt for re-verification
		CStringA token = CGetSetOptions::GetCloudDeviceToken();
		if (token.IsEmpty())
		{
MessageBox(theApp.m_Language.GetString("CloudMsgLoginRequired", "Please log in first before re-entering encryption password."),
			theApp.m_Language.GetString("CloudTitleEncryptionChanged", "Cloud Sync - Encryption Password Changed"), MB_ICONWARNING);
			return 0;
		}

		CInputBox dlg;
		dlg.m_csTitle = theApp.m_Language.GetString("CloudTitleReEnterPassword",
			_T("Encryption Password Required"));
		dlg.m_csPrompt = theApp.m_Language.GetString("CloudPromptReEnterPassword",
			_T("The encryption password has changed on another device.\n\n"
			   "Please enter the new encryption password to continue syncing:"));
		dlg.m_csInput = _T("");
		if (dlg.DoModal() != IDOK || dlg.m_csInput.IsEmpty())
			return 0;

		CString password = dlg.m_csInput;

		EncryptionSetupResult result = CCloudEncryption::ReVerifyPassword(
			m_csServerUrl, CString(token), password);

		if (result.success)
		{
			MessageBox(theApp.m_Language.GetString("CloudMsgPasswordVerified",
				_T("Password verified successfully!\n\nCloud sync will now resume with the updated encryption key.")),
				theApp.m_Language.GetString("CloudTitleEncryptionChanged", _T("Encryption Password Changed")),
				MB_ICONINFORMATION);

			PostMessage(WM_CLOUD_REINIT_SYNC, 0, 0);
		}
		else
		{
			MessageBox(result.error,
				theApp.m_Language.GetString("CloudTitleEncryptionChanged", _T("Encryption Password Changed")),
				MB_ICONERROR);
		}
	}
	else if (statusCode == 999)
	{
		// Encryption initialization failed
msg = theApp.m_Language.GetString("CloudMsgEncryptionFailed",
			"Encryption initialization failed!\n\n"
			"Warning: Your clipboard data will NOT be encrypted.\n\n"
			"Possible causes:\n"
			"- Encryption password not set\n"
			"- Key file not imported\n"
			"- Encryption service unavailable\n\n"
			"Please re-enable encryption in the Cloud Sync settings to protect your data.");

		MessageBox(msg, theApp.m_Language.GetString("CloudTitleEncryptionFailed", "Cloud Sync - Encryption Failed"), MB_ICONWARNING | MB_OK);
		
		// Open this property page to show encryption settings
		CPropertySheet* pSheet = static_cast<CPropertySheet*>(GetParent());
		if (pSheet != nullptr)
		{
			pSheet->SetActivePage(this);
		}
	}
	else if (statusCode == 997)
	{
		// Encryption key (DEK) lost or corrupted — offer recovery
		CString msg = theApp.m_Language.GetString("CloudMsgEncryptionKeyLost",
			_T("Encryption key is missing or corrupted!\n\n"
			   "Your clipboard encryption key (DEK) could not be loaded.\n"
			   "Cloud sync has been stopped to prevent data loss.\n\n"
			   "Do you want to re-setup encryption now?\n"
			   "WARNING: Previously encrypted clips on the server will\n"
			   "become unreadable and will be skipped during sync.\n\n"
			   "- Click Yes to enter a new encryption password and restore sync\n"
			   "- Click No to keep sync stopped (you can re-enable later in settings)"));

		CString title = theApp.m_Language.GetString("CloudTitleEncryptionKeyLost",
			_T("Cloud Sync - Encryption Key Lost"));

		int ret = MessageBox(msg, title, MB_ICONERROR | MB_YESNO);

		// Clear the persistent recovery flag regardless of choice
		CGetSetOptions::SetCloudEncryptionNeedsRecovery(FALSE);

		if (ret == IDYES)
		{
			// Open this property page to show encryption settings
			CPropertySheet* pSheet = static_cast<CPropertySheet*>(GetParent());
			if (pSheet != nullptr)
			{
				pSheet->SetActivePage(this);
			}
		}
	}
	
	return 0;
}

// ---------------------------------------------------------------------------
// OnReinitSync: Async handler for deferred ReinitializeSync
// Called via PostMessage from OnBtnEnableEncryption to avoid UI thread
// blocking while Stop() waits for the sync thread to exit.
// ---------------------------------------------------------------------------
LRESULT COptionCloud::OnReinitSync(WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(wParam);
	UNREFERENCED_PARAMETER(lParam);

	if (!theApp.m_CloudSyncManager.ReinitializeSync())
	{
		OutputDebugStringA("[OptionCloud] OnReinitSync: ReinitializeSync failed.\n");

		MessageBox(theApp.m_Language.GetString("CloudMsgEncryptionEnabledNoSync",
			"Encryption has been enabled, but cloud sync could not be started.\n\n"
			"Please check your login status and try again."),
			theApp.m_Language.GetString("CloudTitleEnableEncryption", "Enable Encryption"), MB_ICONWARNING);
	}

	return 0;
}
