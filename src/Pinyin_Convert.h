#pragma once
#include <string>
#include <vector>
#include <utility>
#include <afx.h>

class CPinyinConvert
{
public:
    CPinyinConvert();
    virtual ~CPinyinConvert();

    std::string ConvertToPinyin(const std::wstring& text);
    std::string ConvertToAbbreviation(const std::wstring& text);
    bool IsAlphaQuery(const std::wstring& s) const;
    CString ExtractAlpha(const CString& input) const;
    static const char* LookupPinyin(wchar_t ch);
    static std::pair<CString, CString> TextToPinyin(const CString& text);

private:
};