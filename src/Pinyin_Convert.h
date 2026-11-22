#pragma once
#include <string>
#include <vector>
#include <utility>
#include <afx.h>

// Single source of truth for pinyin index streams. The database columns
// (pinyin / pinyinAbbr) and the list highlight offsets are both derived from
// this structure so they can never drift apart.
struct PinyinStreams
{
	// Full pinyin stream: preferred (first) reading of each character,
	// lowercase, pinyin letters only (spaces/punctuation/digits skipped).
	std::string pinyin;
	// For each input character: how many bytes it contributes to pinyin.
	std::vector<int> pinyinCharLens;

	// Abbreviation variant: first letter of each character's reading.
	// Variant 0 uses the preferred reading; polyphonic characters expand into
	// additional variants so e.g. "重庆" can also be found by "cq".
	struct AbbrVariant
	{
		std::string stream;
		std::vector<int> charLens;
	};
	std::vector<AbbrVariant> abbrVariants;

	// Column value for pinyinAbbr: all variant streams joined by spaces.
	std::string AbbrColumn() const
	{
		std::string result;
		for (size_t i = 0; i < abbrVariants.size(); i++)
		{
			if (i > 0)
				result += ' ';
			result += abbrVariants[i].stream;
		}
		return result;
	}
};

class CPinyinConvert
{
public:
	// Builds the pinyin streams for text (only the first maxChars characters).
	static PinyinStreams BuildStreams(const CString& text, int maxChars = 2000);

	// Returns (pinyin column value, pinyinAbbr column value).
	static std::pair<CString, CString> TextToPinyin(const CString& text);

	static CString ExtractAlpha(const CString& input);

private:
	static const char* LookupPinyin(wchar_t ch);
};
