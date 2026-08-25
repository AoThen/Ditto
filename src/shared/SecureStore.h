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
#include <atlenc.h>
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
	static CString Protect(LPCTSTR plaintext)
	{
		if (plaintext == NULL || plaintext[0] == _T('\0'))
			return _T("");

		DATA_BLOB input;
		input.pbData = (BYTE*)plaintext;
		input.cbData = (int)(_tcslen(plaintext) * sizeof(TCHAR));

		DATA_BLOB output;
		::SecureZeroMemory(&output, sizeof(output));

		if (!CryptProtectData(&input, L"Ditto.Credential", NULL, NULL, NULL, 0, &output))
			return _T("");

		// Base64-encode the blob so it stays a clean registry REG_SZ value.
		int cchEncoded = ATL::Base64EncodeGetRequiredLength(output.cbData);
		CStringA encodedA;
		ATL::Base64Encode(output.pbData, output.cbData, encodedA.GetBuffer(cchEncoded), &cchEncoded);
		encodedA.ReleaseBuffer(cchEncoded);

		::SecureZeroMemory(output.pbData, output.cbData);
		LocalFree(output.pbData);

		CString result(_T("DPAPI:"));
		result += CString(encodedA);
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

		int cbDecoded = ATL::Base64DecodeGetRequiredLength(encodedA.GetLength());
		if (cbDecoded <= 0)
			return _T("");

		std::vector<BYTE> decoded(cbDecoded);
		int cbActual = 0;
		if (!ATL::Base64Decode(encodedA, encodedA.GetLength(), &decoded[0], &cbActual) || cbActual <= 0)
			return _T("");

		DATA_BLOB input;
		input.pbData = &decoded[0];
		input.cbData = cbActual;

		DATA_BLOB output;
		::SecureZeroMemory(&output, sizeof(output));

		if (!CryptUnprotectData(&input, NULL, NULL, NULL, NULL, 0, &output))
			return _T("");

		::SecureZeroMemory(&decoded[0], cbActual);

		CString plain((TCHAR*)output.pbData, output.cbData / sizeof(TCHAR));
		::SecureZeroMemory(output.pbData, output.cbData);
		LocalFree(output.pbData);

		return plain;
	}
};
