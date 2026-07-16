#pragma once
#include <afxdlgs.h>  // CPropertyPage

// Cloud sync options dialog resource ID
#define IDD_OPTIONS_CLOUD  2200

// Control IDs for cloud sync dialog
#define IDC_CLOUD_ENABLE        2201
#define IDC_CLOUD_PUSH_ON_COPY  2230
#define IDC_CLOUD_PERIODIC_SYNC 2231
#define IDC_CLOUD_SYNC_INTERVAL 2232
#define IDC_CLOUD_STATIC_INTERVAL_LBL 2233
#define IDC_CLOUD_SYNC_STATUS   2234
#define IDC_CLOUD_STATIC_SYNC_STAT 2235
#define IDC_CLOUD_SERVER_URL    2203
#define IDC_CLOUD_USERNAME      2204
#define IDC_CLOUD_PASSWORD      2205
#define IDC_CLOUD_STATUS        2206
#define IDC_CLOUD_BTN_LOGIN     2207
#define IDC_CLOUD_BTN_REGISTER  2208

// Encryption control IDs
#define IDC_CLOUD_BTN_ENABLE_ENCRYPTION  2209
#define IDC_CLOUD_BTN_TEST_ENCRYPTION    2210
#define IDC_CLOUD_ENCRYPTION_PASSWORD    2211
#define IDC_CLOUD_ENCRYPTION_STATUS      2212

// Key export/import control IDs
#define IDC_CLOUD_BTN_EXPORT_KEY       2213
#define IDC_CLOUD_BTN_IMPORT_KEY       2214
#define IDC_CLOUD_KEY_FILE_PATH        2215

// Static text control IDs for language system
#define IDC_CLOUD_STATIC_SERVER_URL    2216
#define IDC_CLOUD_STATIC_USERNAME      2217
#define IDC_CLOUD_STATIC_PASSWORD      2218
#define IDC_CLOUD_STATIC_STATUS        2219
#define IDC_CLOUD_STATIC_ENCRYPT_PWD   2220
#define IDC_CLOUD_STATIC_ENCRYPT_STAT  2221
#define IDC_CLOUD_STATIC_WARNING       2222
#define IDC_CLOUD_STATIC_KEY_PATH      2223
#define IDC_CLOUD_GRP_SETTINGS         2224
#define IDC_CLOUD_GRP_ENCRYPTION       2225
#define IDC_CLOUD_GRP_KEY_FILE         2226

// Force sync control IDs
#define IDC_CLOUD_GRP_FORCE            2227
#define IDC_CLOUD_FORCE_DOWNLOAD       2228
#define IDC_CLOUD_FORCE_UPLOAD         2229

// Rebuild pinyin index
#define IDC_REBUILD_PINYIN  2236

class COptionCloud : public CPropertyPage
{
	DECLARE_DYNCREATE(COptionCloud)

public:
	COptionCloud();
	~COptionCloud();

	enum { IDD = IDD_OPTIONS_CLOUD };

	// Control variables (will be wired to resource later)
	BOOL    m_bEnabled;
	BOOL    m_bPushOnCopy;
	BOOL    m_bPeriodicSync;
	CString m_csSyncInterval;
	CString m_csSyncStatus;
	CString m_csServerUrl;
	CString m_csUsername;
	CString m_csPassword;
	CString m_csStatus;
	CString m_csEncryptionPassword;
	CString m_csEncryptionStatus;
	BOOL    m_bEncryptionEnabled;
	CString m_csKeyFilePath;
	UINT_PTR m_nStatusTimer;

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual BOOL OnApply();
	virtual BOOL OnSetActive();
	virtual BOOL OnKillActive();

	CString m_csTitle;

	DECLARE_MESSAGE_MAP()

	afx_msg void OnTimer(UINT_PTR nIDEvent);
	void RefreshSyncStatus();

	afx_msg void OnBtnLogin();
	afx_msg void OnBtnRegister();
	afx_msg void OnBtnEnableEncryption();
	afx_msg void OnBtnTestEncryption();
	afx_msg void OnBtnExportKey();
	afx_msg void OnBtnImportKey();
	afx_msg void OnBtnForceDownload();
	afx_msg void OnBtnForceUpload();
	afx_msg void OnRebuildPinyinIndex();

	// Handler for cloud authentication required message
	afx_msg LRESULT OnCloudAuthRequired(WPARAM wParam, LPARAM lParam);

	// Async reinitialize sync (avoids UI thread blocking during Stop)
	afx_msg LRESULT OnReinitSync(WPARAM wParam, LPARAM lParam);
};


