// SecureStore.h - DPAPI-backed protection for credentials stored in the registry.
//
// Values written through this class are stored as "DPAPI:" + Base64(CryptProtectData).
// Unprotect() transparently returns values that were stored in plain text by older
// versions, so existing installations keep working and are upgraded on next write.
//
// Header-only by design: src/shared has no precompiled-header setup of its own.

#pragma once

#include <windows.h>
#include <wincrypt.h>
#include <atlstr.h>
#include <vector>

#pragma comment(lib, "crypt32.lib")

class CSecureStore
{
public:
	static bool IsProtected(LPCTSTR stored)
	{
		return stored != NULL && _tcsncmp(stored, _T("DPAPI:"), 6) == 0;
	}

	// Encrypts plaintext with DPAPI (per-user scope).
	// Returns "DPAPI:<base64>" or "" on empty input / failure.
	// pOk is set to false ONLY when DPAPI failed (to distinguish from a
	// legitimate empty input, which is a valid "clear the value" request).
	static CString Protect(LPCTSTR plaintext, bool* pOk = nullptr)
	{
		if (plaintext == NULL || plaintext[0] == _T('\0'))
		{
			if (pOk) *pOk = true;
			return _T("");
		}

		DATA_BLOB input;
		input.pbData = (BYTE*)plaintext;
		input.cbData = (int)(_tcslen(plaintext) * sizeof(TCHAR));

		DATA_BLOB output;
		::SecureZeroMemory(&output, sizeof(output));

		if (!CryptProtectData(&input, L"Ditto.Credential", NULL, NULL, NULL, 0, &output))
		{
			if (pOk) *pOk = false;
			return _T("");
		}

		// Base64-encode the blob so it stays a clean registry REG_SZ value.
		// crypt32 base64 is used because ATL::Base64Decode rejects the
		// output ATL::Base64Encode produces (see TestSecureStore diagnostics).
		// NOCRLF keeps the registry value on a single line.
		DWORD cchEncoded = 0;
		if (!::CryptBinaryToStringA(output.pbData, output.cbData,
			CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &cchEncoded) || cchEncoded == 0)
		{
			::SecureZeroMemory(output.pbData, output.cbData);
			LocalFree(output.pbData);
			if (pOk) *pOk = false;
			return _T("");
		}

		CStringA encodedA;
		::CryptBinaryToStringA(output.pbData, output.cbData,
			CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, encodedA.GetBuffer(cchEncoded), &cchEncoded);
		encodedA.ReleaseBuffer(-1);

		::SecureZeroMemory(output.pbData, output.cbData);
		LocalFree(output.pbData);

		CString result(_T("DPAPI:"));
		result += CString(encodedA);
		if (pOk) *pOk = true;
		return result;
	}

	// Decrypts "DPAPI:"-prefixed values via DPAPI.
	// Values without the prefix are returned unchanged (legacy plain text).
	// Returns "" if decryption fails.
	static CString Unprotect(LPCTSTR stored)
	{
		if (stored == NULL || stored[0] == _T('\0'))
			return _T("");

		if (!IsProtected(stored))
			return stored; // legacy plain-text value

		CStringA encodedA = CT2A(stored + 6, CP_UTF8);
		// Tolerate legacy values that were encoded with line breaks.
		encodedA.Remove('\r');
		encodedA.Remove('\n');

		DWORD cbDecoded = 0;
		if (!::CryptStringToBinaryA(encodedA, encodedA.GetLength(), CRYPT_STRING_BASE64,
			NULL, &cbDecoded, NULL, NULL) || cbDecoded == 0)
			return _T("");

		std::vector<BYTE> decoded(cbDecoded);
		if (!::CryptStringToBinaryA(encodedA, encodedA.GetLength(), CRYPT_STRING_BASE64,
			&decoded[0], &cbDecoded, NULL, NULL) || cbDecoded <= 0)
			return _T("");

		DATA_BLOB input;
		input.pbData = &decoded[0];
		input.cbData = (int)cbDecoded;

		DATA_BLOB output;
		::SecureZeroMemory(&output, sizeof(output));

		if (!CryptUnprotectData(&input, NULL, NULL, NULL, NULL, 0, &output))
			return _T("");

		::SecureZeroMemory(&decoded[0], cbDecoded);

		CString plain((TCHAR*)output.pbData, output.cbData / sizeof(TCHAR));
		::SecureZeroMemory(output.pbData, output.cbData);
		LocalFree(output.pbData);

		return plain;
	}
};
