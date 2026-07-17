#include "stdafx.h"
#include "Pinyin_Convert.h"
#include "pinyin_data.inc"

CPinyinConvert::CPinyinConvert()
{
}

CPinyinConvert::~CPinyinConvert()
{
}

bool CPinyinConvert::IsAlphaQuery(const std::wstring& s) const
{
    if (s.empty()) return false;
    for (size_t i = 0; i < s.length(); i++)
    {
        wchar_t c = s[i];
        if (!((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z')))
            return false;
    }
    return true;
}

CString CPinyinConvert::ExtractAlpha(const CString& input) const
{
    CString result;
    for (int i = 0; i < input.GetLength(); i++)
    {
        TCHAR c = input[i];
        if ((c >= _T('a') && c <= _T('z')) || (c >= _T('A') && c <= _T('Z')))
            result += c;
    }
    return result;
}

const char* CPinyinConvert::LookupPinyin(wchar_t ch)
{
    if (ch < kPinyinTableFirst || ch > kPinyinTableLast)
        return NULL;

    int lo = 0;
    int hi = kPinyinTableSize - 1;
    while (lo <= hi)
    {
        int mid = (lo + hi) / 2;
        if (kPinyinTable[mid].ch == ch)
            return kPinyinTable[mid].py;
        else if (kPinyinTable[mid].ch < ch)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return NULL;
}

std::string CPinyinConvert::ConvertToAbbreviation(const std::wstring& text)
{
    std::string result;
    for (size_t i = 0; i < text.length(); i++)
    {
        wchar_t ch = text[i];
        const char* py = LookupPinyin(ch);
        if (py != NULL && py[0] != '\0')
            result += py[0];
        else if (ch >= L'a' && ch <= L'z')
            result += (char)ch;
        else if (ch >= L'A' && ch <= L'Z')
            result += (char)(ch - L'A' + L'a');
    }
    return result;
}

std::string CPinyinConvert::ConvertToPinyin(const std::wstring& text)
{
    std::string result;
    for (size_t i = 0; i < text.length(); i++)
    {
        wchar_t ch = text[i];
        const char* py = LookupPinyin(ch);
        if (py != NULL)
        {
            result += py;
        }
else
		{
			char buf[8] = {0};
			int len = WideCharToMultiByte(CP_UTF8, 0, &ch, 1, buf, 8, NULL, NULL);
			if (len > 0)
				result += buf;
		}
    }
    return result;
}

std::pair<CString, CString> CPinyinConvert::TextToPinyin(const CString& text)
{
	std::wstring wText(text.GetString());
	CPinyinConvert conv;
	std::string pinyin = conv.ConvertToPinyin(wText);
	std::string abbr = conv.ConvertToAbbreviation(wText);
	return std::make_pair(CA2T(pinyin.c_str(), CP_UTF8), CA2T(abbr.c_str(), CP_UTF8));
}