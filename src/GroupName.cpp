// GroupName.cpp : implementation file
//

#include "stdafx.h"
#include "cp_main.h"
#include "GroupName.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CGroupName dialog


CGroupName::CGroupName(CWnd* pParent /*=NULL*/)
	: CDialog(CGroupName::IDD, pParent)
{
	//{{AFX_DATA_INIT(CGroupName)
	m_csName = _T("");
	m_csDescription = _T("");
	m_groupId = -1;
	//}}AFX_DATA_INIT
}


void CGroupName::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CGroupName)
	DDX_Text(pDX, IDC_NAME, m_csName);
	DDX_Text(pDX, IDC_GROUP_DESCRIPTION, m_csDescription);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CGroupName, CDialog)
	//{{AFX_MSG_MAP(CGroupName)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CGroupName message handlers

void CGroupName::OnOK() 
{
	UpdateData(TRUE);
	
	if (m_groupId > 0)
	{
		try
		{
			CppSQLite3Query q = theApp.m_db.execQueryEx(_T("SELECT mText, m_Description FROM Main WHERE lID = %d"), m_groupId);
			if (!q.eof())
			{
				CString origName = q.getStringField(_T("mText"));
				CString origDesc = q.getStringField(_T("m_Description"));
				if (m_csName != origName || m_csDescription != origDesc)
				{
					CString escName = m_csName;
					CString escDesc = m_csDescription;
					escName.Replace(_T("'"), _T("''"));
					escDesc.Replace(_T("'"), _T("''"));
					theApp.m_db.execDMLEx(_T("UPDATE Main SET mText = '%s', m_Description = '%s', lModifiedDate = %lld WHERE lID = %d"),
						escName, escDesc, CTime::GetCurrentTime().GetTime(), m_groupId);
				}
			}
		}
		CATCH_SQLITE_EXCEPTION
	}

	CDialog::OnOK();
}

BOOL CGroupName::OnInitDialog() 
{
	CDialog::OnInitDialog();

	CWnd *pWnd = GetDlgItem(IDC_NAME);
	if(pWnd)
		pWnd->SetFocus();
		
	return FALSE;
}
