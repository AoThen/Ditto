#include "stdafx.h"
#include "Pinyin_Convert.h"
#include "pinyin_data.inc"
#include <string.h>
#include <ctype.h>

namespace
{
	// Polyphonic abbreviation expansion limits: only characters within the
	// first kAbbrExpandWindow positions are expanded, each polyphonic
	// character contributes at most kMaxInitialsPerChar distinct initials,
	// and no more than kMaxAbbrVariants variants exist at any time.
	const int kAbbrExpandWindow = 64;
	const int kMaxAbbrVariants = 16;
	const int kMaxInitialsPerChar = 2;
}

CString CPinyinConvert::ExtractAlpha(const CString& input)
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

PinyinStreams CPinyinConvert::BuildStreams(const CString& text, int maxChars)
{
	PinyinStreams out;
	int n = text.GetLength();
	if (n > maxChars)
		n = maxChars;

	out.pinyin.reserve((size_t)n * 4 + 1);
	out.pinyinCharLens.assign(n, 0);

	out.abbrVariants.emplace_back();
	out.abbrVariants[0].stream.reserve((size_t)n + 1);
	out.abbrVariants[0].charLens.assign(n, 0);

	for (int i = 0; i < n; i++)
	{
		wchar_t ch = text[i];
		const char* py = LookupPinyin(ch);
		if (py != NULL && py[0] != '\0')
		{
			// Preferred reading = first '|'-separated entry of the table row.
			const char* bar = strchr(py, '|');
			int preferredLen = bar ? (int)(bar - py) : (int)strlen(py);
			out.pinyin.append(py, preferredLen);
			out.pinyinCharLens[i] = preferredLen;

			// Distinct initials of the readings, capped at two.
			char initials[kMaxInitialsPerChar];
			int initialCount = 0;
			const char* p = py;
			while (*p != '\0' && initialCount < kMaxInitialsPerChar)
			{
				char c = (char)tolower((unsigned char)*p);
				bool dup = false;
				for (int k = 0; k < initialCount; k++)
				{
					if (initials[k] == c)
					{
						dup = true;
						break;
					}
				}
				if (!dup)
					initials[initialCount++] = c;
				p = strchr(p, '|');
				if (p == NULL)
					break;
				p++;
			}

			for (size_t v = 0; v < out.abbrVariants.size(); v++)
			{
				out.abbrVariants[v].stream += initials[0];
				out.abbrVariants[v].charLens[i] = 1;
			}

			// Expand polyphonic characters into extra abbreviation variants.
			if (initialCount > 1 && i < kAbbrExpandWindow &&
				(int)out.abbrVariants.size() < kMaxAbbrVariants)
			{
				int room = kMaxAbbrVariants - (int)out.abbrVariants.size();
				int toAdd = min(room, (int)out.abbrVariants.size());
				int base = (int)out.abbrVariants.size();
				for (int j = 0; j < toAdd; j++)
					out.abbrVariants.push_back(out.abbrVariants[j]);
				for (int j = 0; j < toAdd; j++)
				{
					PinyinStreams::AbbrVariant& nv = out.abbrVariants[base + j];
					// The copied variant ends with initials[0] for this char;
					// swap it to the alternate reading's initial.
					nv.stream[nv.stream.size() - 1] = initials[1];
					nv.charLens[i] = 1;
				}
			}
		}
		else if ((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z'))
		{
			char c = (ch >= L'A' && ch <= L'Z') ? (char)(ch - L'A' + L'a') : (char)ch;
			out.pinyin += c;
			out.pinyinCharLens[i] = 1;
			for (size_t v = 0; v < out.abbrVariants.size(); v++)
			{
				out.abbrVariants[v].stream += c;
				out.abbrVariants[v].charLens[i] = 1;
			}
		}
		// Everything else (spaces, punctuation, digits, surrogate pairs)
		// contributes to neither stream and keeps a char length of 0.
	}

	return out;
}

std::pair<CString, CString> CPinyinConvert::TextToPinyin(const CString& text)
{
	PinyinStreams streams = BuildStreams(text);
	std::string abbr = streams.AbbrColumn();
	return std::make_pair(CString(CA2T(streams.pinyin.c_str(), CP_UTF8)),
		CString(CA2T(abbr.c_str(), CP_UTF8)));
}
