//
// GdipButton.cpp : Version 1.0 - see article at CodeProject.com
//
// Author:  Darren Sessions
//          
//
// Description:
//     GdipButton is a CButton derived control that uses GDI+ 
//     to support alternate image formats
//
// History
//     Version 1.0 - 2008 June 10
//     - Initial public release
//
// License:
//     This software is released under the Code Project Open License (CPOL),
//     which may be found here:  http://www.codeproject.com/info/eula.aspx
//     You are free to use this software in any way you like, except that you 
//     may not sell this source code.
//
//     This software is provided "as is" with no expressed or implied warranty.
//     I accept no liability for any damage or loss of business that this 
//     software may cause.
//
///////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "GdipButton.h"

#include "CGdiPlusBitmap.h"
#include "MemDC.h"
#include "CP_Main.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CGdipButton

CGdipButton::CGdipButton()
{
	m_pStdImage = NULL;
	m_pAltImage = NULL;

	m_bHaveBitmaps = FALSE;
	m_bHaveAltImage = FALSE;

	m_pCurBtn = NULL;

	m_bIsDisabled = FALSE;
	m_bIsToggle = FALSE;

	m_bIsHovering = FALSE;
	m_bIsTracking = FALSE;

	m_nCurType = STD_TYPE;

	m_bDarkMode = FALSE;

	m_pToolTip = NULL;

}

CGdipButton::~CGdipButton()
{
	if(m_pStdImage) delete m_pStdImage;
	if(m_pAltImage) delete m_pAltImage;
	if(m_pToolTip)	delete m_pToolTip;
}


BEGIN_MESSAGE_MAP(CGdipButton, CButton)
	//{{AFX_MSG_MAP(CGdipButton)
	ON_WM_DRAWITEM()
	ON_WM_ERASEBKGND()
	ON_WM_CTLCOLOR_REFLECT()
	ON_WM_MOUSEMOVE()
	ON_MESSAGE(WM_MOUSELEAVE, OnMouseLeave)
	ON_MESSAGE(WM_MOUSEHOVER, OnMouseHover)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()


BOOL CGdipButton::LoadStdImageDPI(int dpi, UINT id96, UINT id120, UINT id144, UINT id168, UINT id192, LPCTSTR pType, UINT id225, UINT id250, UINT id275, UINT id300, UINT id325, UINT id350)
{
	BOOL ret = FALSE;

	if (dpi >= 336 && id350 != 0)
	{
		ret = LoadStdImage(id350, pType);
	}
	else if (dpi >= 312 && id325 != 0)
	{
		ret = LoadStdImage(id325, pType);
	}
	else if (dpi >= 288 && id300 != 0)
	{
		ret = LoadStdImage(id300, pType);
	}
	else if (dpi >= 264 && id275 != 0)
	{
		ret = LoadStdImage(id275, pType);
	}
	else if (dpi >= 240 && id250 != 0)
	{
		ret = LoadStdImage(id250, pType);
	}
	else if (dpi >= 216 && id225 != 0)
	{
		ret = LoadStdImage(id225, pType);
	}
	else if (dpi >= 192)
	{
		ret = LoadStdImage(id192, pType);
	}
	else if (dpi >= 168)
	{
		ret = LoadStdImage(id168, pType);
	}
	else if (dpi >= 144)
	{
		ret = LoadStdImage(id144, pType);
	}
	else if (dpi >= 120)
	{
		ret = LoadStdImage(id120, pType);
	}
	else
	{
		ret = LoadStdImage(id96, pType);
	}

	return ret;
}

//=============================================================================
//
// LoadStdImage()
//
// Purpose:     The LoadStdImage() Loads the image for the button.  This 
//				function must be called at a minimum or the button wont do 
//				anything.
//
// Parameters:  
//		[IN]	id
//				resource id, one of the resources already imported with the 
//				resource editor, usually begins with IDR_  
//
//		[IN]	pType
//				pointer to string describing the resource type
//				
// Returns:     BOOL
//				Non zero if successful, otherwise zero
//
//=============================================================================
BOOL CGdipButton::LoadStdImage(UINT id, LPCTSTR pType)
{
	m_pStdImage = new CGdiPlusBitmapResource;
	return m_pStdImage->Load(id, pType);
}

void CGdipButton::Reset()
{
	delete m_pStdImage;
	m_pStdImage = NULL;
	delete m_pAltImage;
	m_pAltImage = NULL;
	delete m_pToolTip;
	m_pToolTip = NULL;

	m_bHaveBitmaps = FALSE;
	m_bHaveAltImage = FALSE;

	m_dcStd.DeleteDC();
	m_dcStdP.DeleteDC();
	m_dcStdH.DeleteDC();
	m_dcBk.DeleteDC();

	m_dcGS.DeleteDC();
	m_dcAlt.DeleteDC();
	m_dcAltP.DeleteDC();
	m_dcAltH.DeleteDC();

	m_bmpBk.DeleteObject();
	m_bmpStd.DeleteObject();
	m_bmpStdP.DeleteObject();
	m_bmpStdH.DeleteObject();
	m_bmpGS.DeleteObject();
	m_bmpAlt.DeleteObject();
	m_bmpAltP.DeleteObject();
	m_bmpAltH.DeleteObject();
}

void CGdipButton::ClearBitmaps()
{
	m_dcBk.DeleteDC();
	m_dcStd.DeleteDC();
	m_dcStdP.DeleteDC();
	m_dcStdH.DeleteDC();
	m_dcAlt.DeleteDC();
	m_dcAltP.DeleteDC();
	m_dcAltH.DeleteDC();
	m_dcGS.DeleteDC();

	m_bmpBk.DeleteObject();
	m_bmpStd.DeleteObject();
	m_bmpStdP.DeleteObject();
	m_bmpStdH.DeleteObject();
	m_bmpGS.DeleteObject();
	m_bmpAlt.DeleteObject();
	m_bmpAltP.DeleteObject();
	m_bmpAltH.DeleteObject();

	m_bHaveBitmaps = FALSE;
}

void CGdipButton::Test(CString c)
{
	m_pStdImage = new CGdiPlusBitmapResource;
	m_pStdImage->Loads(c);
}

//=============================================================================
//
// LoadAltImage()
//
// Purpose:     The LoadAltImage() Loads the altername image for the button.  
//				This function call is optional
// Parameters:  
//		[IN]	id
//				resource id, one of the resources already imported with the 
//				resource editor, usually begins with IDR_  
//
//		[IN]	pType
//				pointer to string describing the resource type
//				
// Returns:     BOOL
//				Non zero if successful, otherwise zero
//
//=============================================================================
BOOL CGdipButton::LoadAltImage(UINT id, LPCTSTR pType)
{
	m_bHaveAltImage = TRUE;
	m_pAltImage = new CGdiPlusBitmapResource;
	return (m_pAltImage->Load(id, pType));
}


//=============================================================================
//
//	The framework calls this member function when a child control is about to 
//	be drawn.  All the bitmaps are created here on the first call. Every thing
//	is done with a memory DC except the background, which get's it's information 
//	from the parent. The background is needed for transparent portions of PNG 
//	images. An always on top app (such as Task Manager) that is in the way can 
//	cause it to get an incorrect background.  To avoid this, the parent should 
//	call the SetBkGnd function with a memory DC when it creates the background.
//				
//=============================================================================
HBRUSH CGdipButton::CtlColor(CDC* pScreenDC, UINT nCtlColor) 
{
	if(!m_bHaveBitmaps)
	{
		if(!m_pStdImage)
		{
			return NULL; // Load the standard image with LoadStdImage()
		}

		CRect rect;
		GetClientRect(rect);

		// do everything with mem dc
		CMemDCEx pDC(pScreenDC, rect);

		Gdiplus::Graphics graphics(pDC->m_hDC);

		// background
		if (m_dcBk.m_hDC == NULL)
		{
			CRect rect1;
			CClientDC clDC(GetParent());
			GetWindowRect(rect1);
			GetParent()->ScreenToClient(rect1);

			m_dcBk.CreateCompatibleDC(&clDC);
			m_bmpBk.CreateCompatibleBitmap(&clDC, rect.Width(), rect.Height());
			m_dcBk.SelectObject(&m_bmpBk);
			m_dcBk.BitBlt(0, 0, rect.Width(), rect.Height(), &clDC, rect1.left, rect1.top, SRCCOPY);
		}

		PaintBk(pDC);

			// --- color matrices ---
			ColorMatrix* pStdMat = NULL;
			ColorMatrix HotMat = {	1.05f, 0.00f, 0.00f, 0.00f, 0.00f,
									0.00f, 1.05f, 0.00f, 0.00f, 0.00f,
									0.00f, 0.00f, 1.05f, 0.00f, 0.00f,
									0.00f, 0.00f, 0.00f, 1.00f, 0.00f,
									0.05f, 0.05f, 0.05f, 0.00f, 1.00f	};
			ColorMatrix GrayMat = {	0.30f, 0.30f, 0.30f, 0.00f, 0.00f,
									0.59f, 0.59f, 0.59f, 0.00f, 0.00f,
									0.11f, 0.11f, 0.11f, 0.00f, 0.00f,
									0.00f, 0.00f, 0.00f, 1.00f, 0.00f,
									0.00f, 0.00f, 0.00f, 0.00f, 1.00f	};
			ImageAttributes iaHot, iaGray;

			if (m_bDarkMode)
			{
				static ColorMatrix DarkMat = {	2.75f, 0.00f, 0.00f, 0.00f, 0.35f,
												0.00f, 2.75f, 0.00f, 0.00f, 0.35f,
												0.00f, 0.00f, 2.75f, 0.00f, 0.35f,
												0.00f, 0.00f, 0.00f, 1.00f, 0.00f,
0.20f, 0.20f, 0.20f, 0.00f, 1.00f };
			static ColorMatrix DarkHotMat = { 2.875f, 0.00f, 0.00f, 0.00f, 0.10f,
													0.00f, 2.875f, 0.00f, 0.00f, 0.10f,
													0.00f, 0.00f, 2.875f, 0.00f, 0.10f,
													0.00f, 0.00f, 0.00f, 1.00f, 0.00f,
													0.00f, 0.00f, 0.00f, 0.00f, 1.00f };
				static ColorMatrix DarkGrayMat = { 0.825f, 0.825f, 0.825f, 0.00f, 0.00f,
													1.6225f, 1.6225f, 1.6225f, 0.00f, 0.00f,
													0.3025f, 0.3025f, 0.3025f, 0.00f, 0.00f,
													0.00f, 0.00f, 0.00f, 1.00f, 0.00f,
													0.00f, 0.00f, 0.00f, 0.00f, 1.00f };
				pStdMat = &DarkMat;
				HotMat = DarkHotMat;
				GrayMat = DarkGrayMat;
			}

			iaHot.SetColorMatrix(&HotMat);
			iaGray.SetColorMatrix(&GrayMat);

			float width = (float)m_pStdImage->m_pBitmap->GetWidth();
			float height = (float)m_pStdImage->m_pBitmap->GetHeight();

			RectF grect; grect.X = 0, grect.Y = 0; grect.Width = width; grect.Height = height;

			ImageAttributes iaStd;
			if (pStdMat) iaStd.SetColorMatrix(pStdMat);
			graphics.DrawImage(*m_pStdImage, grect, 0, 0, width, height, UnitPixel, pStdMat ? &iaStd : NULL);

			if (m_dcStd.m_hDC == NULL)
			{
				m_dcStd.CreateCompatibleDC(pDC);
				m_bmpStd.CreateCompatibleBitmap(pDC, rect.Width(), rect.Height());
				m_dcStd.SelectObject(&m_bmpStd);
				m_dcStd.BitBlt(0, 0, rect.Width(), rect.Height(), pDC, 0, 0, SRCCOPY);
			}

			// standard image pressed
			if (m_dcStdP.m_hDC == NULL)
			{
				PaintBk(pDC);

				//graphics.DrawImage(*m_pStdImage, 1, 1);

				//m_dcStdP.CreateCompatibleDC(pDC);
				//bmp.CreateCompatibleBitmap(pDC, rect.Width(), rect.Height());
				//pOldBitmap = m_dcStdP.SelectObject(&bmp);
				//m_dcStdP.BitBlt(0, 0, rect.Width(), rect.Height(), pDC, 0, 0, SRCCOPY);
				//bmp.DeleteObject();

				float width = (float)m_pStdImage->m_pBitmap->GetWidth();
				float height = (float)m_pStdImage->m_pBitmap->GetHeight();

				RectF grect; grect.X = 0, grect.Y = 0; grect.Width = width; grect.Height = height;

				graphics.DrawImage(*m_pStdImage, grect, -1, -1, width, height, UnitPixel, pStdMat ? &iaStd : NULL);

				m_dcStdP.CreateCompatibleDC(pDC);
				m_bmpStdP.CreateCompatibleBitmap(pDC, rect.Width(), rect.Height());
				m_dcStdP.SelectObject(&m_bmpStdP);
				m_dcStdP.BitBlt(0, 0, rect.Width(), rect.Height(), pDC, 0, 0, SRCCOPY);
			}

			// standard image hot
			if(m_dcStdH.m_hDC == NULL)
			{
				PaintBk(pDC);

				graphics.DrawImage(*m_pStdImage, grect, 0, 0, width, height, UnitPixel, &iaHot);

				m_dcStdH.CreateCompatibleDC(pDC);
				m_bmpStdH.CreateCompatibleBitmap(pDC, rect.Width(), rect.Height());
				m_dcStdH.SelectObject(&m_bmpStdH);
				m_dcStdH.BitBlt(0, 0, rect.Width(), rect.Height(), pDC, 0, 0, SRCCOPY);
			}

			// grayscale image
			if(m_dcGS.m_hDC == NULL)
			{
				PaintBk(pDC);

				graphics.DrawImage(*m_pStdImage, grect, 0, 0, width, height, UnitPixel, &iaGray);

				m_dcGS.CreateCompatibleDC(pDC);
				m_bmpGS.CreateCompatibleBitmap(pDC, rect.Width(), rect.Height());
				m_dcGS.SelectObject(&m_bmpGS);
				m_dcGS.BitBlt(0, 0, rect.Width(), rect.Height(), pDC, 0, 0, SRCCOPY);
			}

		// alternate image
		if( (m_dcAlt.m_hDC == NULL) && m_bHaveAltImage )
		{
			PaintBk(pDC);

			float altW = (float)m_pAltImage->m_pBitmap->GetWidth();
			float altH = (float)m_pAltImage->m_pBitmap->GetHeight();
			RectF altDest(0, 0, altW, altH);
			graphics.DrawImage(*m_pAltImage, altDest, 0, 0, altW, altH, UnitPixel, pStdMat ? &iaStd : NULL);
		
			m_dcAlt.CreateCompatibleDC(pDC);
			m_bmpAlt.CreateCompatibleBitmap(pDC, rect.Width(), rect.Height());
			m_dcAlt.SelectObject(&m_bmpAlt);
			m_dcAlt.BitBlt(0, 0, rect.Width(), rect.Height(), pDC, 0, 0, SRCCOPY);

			// alternate image pressed
			if( (m_dcAltP.m_hDC == NULL) && m_bHaveAltImage )
			{
				PaintBk(pDC);

				float altW = (float)m_pAltImage->m_pBitmap->GetWidth();
				float altH = (float)m_pAltImage->m_pBitmap->GetHeight();
				RectF altDest(1, 1, altW, altH);
				graphics.DrawImage(*m_pAltImage, altDest, 0, 0, altW, altH, UnitPixel, pStdMat ? &iaStd : NULL);
			
				m_dcAltP.CreateCompatibleDC(pDC);
				m_bmpAltP.CreateCompatibleBitmap(pDC, rect.Width(), rect.Height());
				m_dcAltP.SelectObject(&m_bmpAltP);
				m_dcAltP.BitBlt(0, 0, rect.Width(), rect.Height(), pDC, 0, 0, SRCCOPY);
			}

			// alternate image hot
			if(m_dcAltH.m_hDC == NULL)
			{
				PaintBk(pDC);

				float altW = (float)m_pAltImage->m_pBitmap->GetWidth();
				float altH = (float)m_pAltImage->m_pBitmap->GetHeight();
				RectF altDest(0, 0, altW, altH);
				graphics.DrawImage(*m_pAltImage, altDest, 0, 0, altW, altH, UnitPixel, &iaHot);

				m_dcAltH.CreateCompatibleDC(pDC);
				m_bmpAltH.CreateCompatibleBitmap(pDC, rect.Width(), rect.Height());
				m_dcAltH.SelectObject(&m_bmpAltH);
				m_dcAltH.BitBlt(0, 0, rect.Width(), rect.Height(), pDC, 0, 0, SRCCOPY);
			}
		}

		if(m_pCurBtn == NULL)
		{
			m_pCurBtn = &m_dcStd;
		}

		m_bHaveBitmaps = TRUE;
	}

	return NULL;
}

//=============================================================================
// paint the background
//=============================================================================
void CGdipButton::PaintBk(CDC *pDC)
{
	CRect rect;
	GetClientRect(rect);
	pDC->BitBlt(0, 0, rect.Width(), rect.Height(), &m_dcBk, 0, 0, SRCCOPY);
}

//=============================================================================
// paint the bitmap currently pointed to with m_pCurBtn
//=============================================================================
void CGdipButton::PaintBtn(CDC *pDC)
{
	CRect rect;
	GetClientRect(rect);
	pDC->BitBlt(0, 0, rect.Width(), rect.Height(), m_pCurBtn, 0, 0, SRCCOPY);
}

//=============================================================================
// enables the toggle mode
// returns if it doesn't have the alternate image
//=============================================================================
void CGdipButton::EnableToggle(BOOL bEnable)
{
	if(!m_bHaveAltImage) return;

	m_bIsToggle = bEnable; 

	// this actually makes it start in the std state since toggle is called before paint
	if(bEnable)	m_pCurBtn = &m_dcAlt;
	else		m_pCurBtn = &m_dcStd;

}

//=============================================================================
// sets the image type and disabled state then repaints
//=============================================================================
void CGdipButton::SetImage(int type)
{
	m_nCurType = type;

	(type == DIS_TYPE) ? m_bIsDisabled = TRUE : m_bIsDisabled = FALSE;

	Invalidate();
}

//=============================================================================
// set the control to owner draw
//=============================================================================
void CGdipButton::PreSubclassWindow()
{
	// Set control to owner draw
	ModifyStyle(0, BS_OWNERDRAW, SWP_FRAMECHANGED);

	CButton::PreSubclassWindow();
}

//=============================================================================
// disable double click 
//=============================================================================
BOOL CGdipButton::PreTranslateMessage(MSG* pMsg) 
{
	if (pMsg->message == WM_LBUTTONDBLCLK)
		pMsg->message = WM_LBUTTONDOWN;

	if (m_pToolTip != NULL)
	{
		if (::IsWindow(m_pToolTip->m_hWnd))
		{
			m_pToolTip->RelayEvent(pMsg);		
		}
	}

	return CButton::PreTranslateMessage(pMsg);
}


//=============================================================================
// overide the erase function
//=============================================================================
BOOL CGdipButton::OnEraseBkgnd(CDC* pDC) 
{
	return TRUE;
}

//=============================================================================
// Paint the button depending on the state of the mouse
//=============================================================================
void CGdipButton::DrawItem(LPDRAWITEMSTRUCT lpDIS) 
{
	CDC* pDC = CDC::FromHandle(lpDIS->hDC);

	// handle disabled state
	if(m_bIsDisabled)
	{
		m_pCurBtn = &m_dcGS;
		PaintBtn(pDC);
		return;
	}

	BOOL bIsPressed = (lpDIS->itemState & ODS_SELECTED);

	// handle toggle button
	if(m_bIsToggle && bIsPressed)
	{
		(m_nCurType == STD_TYPE) ? m_nCurType = ALT_TYPE : m_nCurType = STD_TYPE;
	}

	if(bIsPressed)
	{
		if(m_nCurType == STD_TYPE)
			m_pCurBtn = &m_dcStdP;
		else
			m_pCurBtn = &m_dcAltP;
	}
	else if(m_bIsHovering)
	{

		if(m_nCurType == STD_TYPE)
			m_pCurBtn = &m_dcStdH;
		else
			m_pCurBtn = &m_dcAltH;
	}
	else
	{
		if(m_nCurType == STD_TYPE)
			m_pCurBtn = &m_dcStd;
		else
			m_pCurBtn = &m_dcAlt;
	}

	// paint the button
	PaintBtn(pDC);
}

//=============================================================================
LRESULT CGdipButton::OnMouseHover(WPARAM wparam, LPARAM lparam) 
//=============================================================================
{
	m_bIsHovering = TRUE;
	Invalidate();
	DeleteToolTip();

	// Create a new Tooltip with new Button Size and Location
	SetToolTipText(m_tooltext);

	if (m_pToolTip != NULL)
	{
		if (::IsWindow(m_pToolTip->m_hWnd))
		{
			//Display ToolTip
			m_pToolTip->Update();
		}
	}

	return 0;
}


//=============================================================================
LRESULT CGdipButton::OnMouseLeave(WPARAM wparam, LPARAM lparam)
//=============================================================================
{
	m_bIsTracking = FALSE;
	m_bIsHovering = FALSE;
	Invalidate();
	return 0;
}

//=============================================================================
void CGdipButton::OnMouseMove(UINT nFlags, CPoint point) 
//=============================================================================
{
	if (!m_bIsTracking)
	{
		TRACKMOUSEEVENT tme;
		tme.cbSize = sizeof(tme);
		tme.hwndTrack = m_hWnd;
		tme.dwFlags = TME_LEAVE|TME_HOVER;
		tme.dwHoverTime = 1;
		m_bIsTracking = _TrackMouseEvent(&tme);
	}
	
	CButton::OnMouseMove(nFlags, point);
}

//=============================================================================
//	
//	Call this member function with a memory DC from the code that paints 
//	the parents background.  Passing the screen DC defeats the purpose of 
//  using this function.
//
//=============================================================================
void CGdipButton::SetBkGnd(CDC* pDC)
{
	CRect rect, rectS;
	CBitmap bmp, *pOldBitmap;

	GetClientRect(rect);
	GetWindowRect(rectS);
	GetParent()->ScreenToClient(rectS);

	m_dcBk.DeleteDC();

	m_dcBk.CreateCompatibleDC(pDC);
	bmp.CreateCompatibleBitmap(pDC, rect.Width(), rect.Height());
	pOldBitmap = m_dcBk.SelectObject(&bmp);
	m_dcBk.BitBlt(0, 0, rect.Width(), rect.Height(), pDC, rectS.left, rectS.top, SRCCOPY);
	m_dcBk.SelectObject(pOldBitmap);
}


//=============================================================================
// Set the tooltip with a string resource
//=============================================================================
void CGdipButton::SetToolTipText(UINT nId, BOOL bActivate)
{
	// load string resource
	m_tooltext.LoadString(nId);

	// If string resource is not empty
	if (m_tooltext.IsEmpty() == FALSE)
	{
		SetToolTipText(m_tooltext, bActivate);
	}

}

//=============================================================================
// Set the tooltip with a CString
//=============================================================================
void CGdipButton::SetToolTipText(CString spText, BOOL bActivate)
{
	// We cannot accept NULL pointer
	if (spText.IsEmpty()) return;

	// Initialize ToolTip
	InitToolTip();
	m_tooltext = spText;

	// If there is no tooltip defined then add it
	if (m_pToolTip->GetToolCount() == 0)
	{
		CRect rectBtn; 
		GetClientRect(rectBtn);
		m_pToolTip->AddTool(this, m_tooltext, rectBtn, 1);
	}

	// Set text for tooltip
	m_pToolTip->UpdateTipText(m_tooltext, this, 1);
	m_pToolTip->SetDelayTime(2000);
	m_pToolTip->Activate(bActivate);
}

//=============================================================================
void CGdipButton::InitToolTip()
//=============================================================================
{
	if (m_pToolTip == NULL)
	{
		m_pToolTip = new CToolTipCtrl;
		// Create ToolTip control
		m_pToolTip->Create(this);
		m_pToolTip->Activate(TRUE);
	}
} 

//=============================================================================
void CGdipButton::DeleteToolTip()
//=============================================================================
{
	// Destroy Tooltip incase the size of the button has changed.
	if (m_pToolTip != NULL)
	{
		delete m_pToolTip;
		m_pToolTip = NULL;
	}
}

void CGdipButton::SetDarkMode(BOOL bDark)
{
	m_bDarkMode = bDark;
	ClearBitmaps();
	Invalidate();
}

