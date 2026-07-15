#pragma once
#include <string>
#include <vector>

class CPinyinConvert
{
public:
    CPinyinConvert();
    virtual ~CPinyinConvert();

    std::string ConvertToPinyin(const std::wstring& text);
    std::string ConvertToAbbreviation(const std::wstring& text);
    bool IsAlphaQuery(const std::wstring& s) const;
    static const char* LookupPinyin(wchar_t ch);

private:
};