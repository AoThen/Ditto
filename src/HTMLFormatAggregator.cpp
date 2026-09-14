#include "stdafx.h"
#include ".\htmlformataggregator.h"
#include "Misc.h"

CHTMLFormatAggregator::CHTMLFormatAggregator(CStringA csSepator) :
	m_csSeparator(csSepator)
{
	m_csSeparator.Replace("\r\n", "<br>");
}

CHTMLFormatAggregator::~CHTMLFormatAggregator(void)
{
}

bool CHTMLFormatAggregator::AddClip(LPVOID lpData, int nDataSize, int nPos, int nCount, UINT cfType)
{
	LPSTR pText = (LPSTR)lpData;
	if(pText == NULL)
	{
		return false;
	}

	//Ensure it's null terminated
	if(pText[nDataSize-1] != '\0')
	{
		pText[nDataSize-1] = NULL;
	}

	CHTMFormatStruct HtmlData;
	if(HtmlData.GetData(pText))
	{
		m_csNewText += HtmlData.GetFragment();

		if(m_csSourceURL.IsEmpty())
			m_csSourceURL = HtmlData.GetURL();
		if(m_csVersion.IsEmpty())
			m_csVersion = HtmlData.GetVersion();

		if(nPos != nCount-1)
		{
			m_csNewText += m_csSeparator;
		}
	}	

	return true;
}

HGLOBAL CHTMLFormatAggregator::GetHGlobal()
{
	CHTMFormatStruct HtmlData;
	HtmlData.SetFragment(m_csNewText);
	HtmlData.SetURL(m_csSourceURL);
	HtmlData.SetVersion(m_csVersion);

	CStringA csHtmlFormat;
	HtmlData.Serialize(csHtmlFormat);

	long lLen = csHtmlFormat.GetLength();
	HGLOBAL hGlobal = NewGlobalP(csHtmlFormat.GetBuffer(lLen), lLen+sizeof(char));
	csHtmlFormat.ReleaseBuffer();

	return hGlobal;
}

bool CHTMFormatStruct::GetData(LPCSTR HTML)
{
	CStringA csHTML(HTML);

	// Drop trailing NUL padding that may have been inserted by AddClip
	int nLen = csHTML.GetLength();
	while (nLen > 0 && csHTML[nLen - 1] == '\0')
		nLen--;
	if (nLen <= 0)
		return false;
	csHTML = csHTML.Left(nLen);

	// Parse the ASCII header lines. All values are byte offsets or strings
	// in the original UTF-8 source, so we must stay in byte space and
	// never convert through the system ANSI codepage.
	int nPos = 0;
	while (nPos < nLen)
	{
		int nLineEnd = csHTML.Find("\r\n", nPos);
		if (nLineEnd < 0)
			nLineEnd = nLen;

		CStringA line = csHTML.Mid(nPos, nLineEnd - nPos);
		int nColon = line.Find(':');
		if (nColon >= 0)
		{
			CStringA csParam = line.Left(nColon);
			csParam.Trim();
			CStringA csValue = line.Mid(nColon + 1);
			csValue.Trim();

			if (csParam.CompareNoCase("Version") == 0)
				m_csVersion = csValue;
			else if (csParam.CompareNoCase("StartHTML") == 0)
				m_lStartHTML = atol(csValue);
			else if (csParam.CompareNoCase("EndHTML") == 0)
				m_lEndHTML = atol(csValue);
			else if (csParam.CompareNoCase("StartFragment") == 0)
				m_lStartFragment = atol(csValue);
			else if (csParam.CompareNoCase("EndFragment") == 0)
				m_lEndFragment = atol(csValue);
			else if (csParam.CompareNoCase("SourceURL") == 0)
			{
				m_csSourceURL = csValue;
				break;
			}
		}
		else if (line.Find("<html") >= 0)
		{
			break;
		}

		if (nLineEnd >= nLen)
			break;
		nPos = nLineEnd + 2;
	}

	if (m_lStartFragment >= 0 && m_lEndFragment >= 0 &&
		m_lStartFragment < m_lEndFragment &&
		m_lEndFragment <= nLen)
	{
		m_csFragment = csHTML.Mid(m_lStartFragment, m_lEndFragment - m_lStartFragment);
		m_csFragment.Trim();
	}

	if (m_csFragment.IsEmpty())
		return false;

	return true;
}

bool CHTMFormatStruct::Serialize(CStringA &csHTMLFormat)
{
	//Build a structure just like this
// Version:0.9
// StartHTML:00000244
// EndHTML:00000338
// StartFragment:00000278
// StartFragment:00000302
// SourceURL:http://www.google.com/search?hl=en&client=firefox-a&channel=s&rls=org.mozilla%3Aen-US%3Aofficial&hs=oIx&q=c%2B%2B+interface&btnG=Search
// <html><body>
// <!--StartFragment--><font size="-1">e</font><!--EndFragment-->
// </body>
// </html>


	CStringA csVersionText("Version:");
	CStringA csStartHTMLText("StartHTML:");
	CStringA csEndHTMLText("EndHTML:");
	CStringA csStartFragmentText("StartFragment:");
	CStringA csEndFragmentText("EndFragment:");
	CStringA csSourceURLText("SourceURL:");
	CStringA csStartFragmentMarkerText("<!--StartFragment-->");
	CStringA csEndFragmentMarkerText("<!--EndFragment-->");
	CStringA csStartHTML("<html><body>");
	CStringA csEndHTML("</body>\r\n</html>");
	long lNumberCharacters = 8;

	//+2 is for the line feeds

	long lCurrentPos = csVersionText.GetLength() + m_csVersion.GetLength() + 2 +
						csStartHTMLText.GetLength() + lNumberCharacters + 2 +
						csEndHTMLText.GetLength() + lNumberCharacters + 2 +
						csStartFragmentText.GetLength() + lNumberCharacters + 2 +
						csEndFragmentText.GetLength() + lNumberCharacters + 2 +
						csSourceURLText.GetLength() + m_csSourceURL.GetLength() + 2;

	m_lStartHTML = lCurrentPos;

	lCurrentPos += csStartHTMLText.GetLength() + 2 + 
					csStartFragmentMarkerText.GetLength() + 2;
	m_lStartFragment = lCurrentPos;

	lCurrentPos += m_csFragment.GetLength();
	m_lEndFragment = lCurrentPos;

	lCurrentPos += csEndFragmentMarkerText.GetLength() + 2 +
					csEndHTML.GetLength();
	m_lEndHTML = lCurrentPos;


	csHTMLFormat = csVersionText + m_csVersion + "\r\n";

	CStringA csFormat;
	csFormat.Format("%s%08d\r\n", csStartHTMLText, m_lStartHTML);
	csHTMLFormat += csFormat;

	csFormat.Format("%s%08d\r\n", csEndHTMLText, m_lEndHTML);
	csHTMLFormat += csFormat;

	csFormat.Format("%s%08d\r\n", csStartFragmentText, m_lStartFragment);
	csHTMLFormat += csFormat;

	csFormat.Format("%s%08d\r\n", csEndFragmentText, m_lEndFragment);
	csHTMLFormat += csFormat;

	csFormat.Format("%s%s\r\n", csSourceURLText, m_csSourceURL);
	csHTMLFormat += csFormat;

	csFormat.Format("%s\r\n%s", csStartHTML, csStartFragmentMarkerText);
	csHTMLFormat += csFormat;

	csHTMLFormat += m_csFragment;

	csFormat.Format("%s\r\n%s", csEndFragmentMarkerText, csEndHTML);
	csHTMLFormat += csFormat;

	return true;
}