#pragma once
#include <afx.h>

struct LoginResult
{
	BOOL success;
	CString deviceToken;
	CString deviceId;
	CString error;
};

class CCloudAuth
{
public:
	// Login to cloud server
	static LoginResult Login(const CString& serverUrl,
	                         const CString& username,
	                         const CString& password);

	// Register new account
	static LoginResult Register(const CString& serverUrl,
	                            const CString& username,
	                            const CString& email,
	                            const CString& password);

	// Check if currently logged in (token exists)
	static BOOL IsLoggedIn();

	// Logout (clear local token)
	static void Logout();
};
