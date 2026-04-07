#pragma once
#include <afxdlgs.h>  // CPropertyPage

// TODO: Define this resource ID in resource.h when wiring up the dialog resource
// Suggested: #define IDD_OPTIONS_CLOUD  2200
#define IDD_OPTIONS_CLOUD  2200

// TODO: Define these control IDs in resource.h when wiring up controls
#define IDC_CLOUD_ENABLE        2201
#define IDC_CLOUD_AUTO_SYNC     2202
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

class COptionCloud : public CPropertyPage
{
	DECLARE_DYNCREATE(COptionCloud)

public:
	COptionCloud();
	~COptionCloud();

	enum { IDD = IDD_OPTIONS_CLOUD };

	// Control variables (will be wired to resource later)
	BOOL    m_bEnabled;
	BOOL    m_bAutoSync;
	CString m_csServerUrl;
	CString m_csUsername;
	CString m_csPassword;
	CString m_csStatus;
	CString m_csEncryptionPassword;
	CString m_csEncryptionStatus;
	BOOL    m_bEncryptionEnabled;

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual BOOL OnApply();

	DECLARE_MESSAGE_MAP()

	afx_msg void OnBtnLogin();
	afx_msg void OnBtnRegister();
	afx_msg void OnBtnEnableEncryption();
	afx_msg void OnBtnTestEncryption();
};
