package tests

import (
	"fmt"
	"net/http"
	"testing"

	"ditto-cloud-server/internal/model"
	"ditto-cloud-server/tests/testutil"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// TestRegister_Success — register new user, expect code=0
func TestRegister_Success(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	statusCode, respBody := testutil.RegisterUser(t, server, "testuser", "testuser@example.com", "password123")

	assert.Equal(t, http.StatusOK, statusCode)
	code, message, data := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)
	assert.Equal(t, "注册成功", message)
	assert.NotNil(t, data)
	assert.Contains(t, data, "user_id")
}

// TestRegister_DuplicateUsername — registration is only allowed for first user.
// After the first user is registered, subsequent registration attempts should be rejected.
func TestRegister_DuplicateUsername(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	// First registration should succeed
	statusCode, respBody := testutil.RegisterUser(t, server, "dupuser", "first@example.com", "password123")
	assert.Equal(t, http.StatusOK, statusCode)
	code, _, _ := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)

	// Second registration with same username should fail (username taken)
	statusCode, respBody = testutil.RegisterUser(t, server, "dupuser", "second@example.com", "password456")
	assert.Equal(t, http.StatusBadRequest, statusCode)
	code, message, _ := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 40001, code)
	assert.Contains(t, message, "用户名已存在")
}

// TestRegister_DuplicateEmail — registration is only allowed for first user.
func TestRegister_DuplicateEmail(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	// First registration should succeed
	statusCode, respBody := testutil.RegisterUser(t, server, "user1", "dupemail@example.com", "password123")
	assert.Equal(t, http.StatusOK, statusCode)
	code, _, _ := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)

	// Second registration with same email should fail (email taken)
	statusCode, respBody = testutil.RegisterUser(t, server, "user2", "dupemail@example.com", "password456")
	assert.Equal(t, http.StatusBadRequest, statusCode)
	code, message, _ := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 40002, code)
	assert.Contains(t, message, "邮箱已被注册")
}

// TestRegister_InvalidInput — empty username/email/password, expect code=40000
func TestRegister_InvalidInput(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	tests := map[string]map[string]string{
		"empty username": {"username": "", "email": "test@example.com", "password": "password123"},
		"empty email":    {"username": "testuser", "email": "", "password": "password123"},
		"empty password": {"username": "testuser", "email": "test@example.com", "password": ""},
		"short username": {"username": "ab", "email": "test@example.com", "password": "password123"},
		"short password": {"username": "testuser", "email": "test@example.com", "password": "abc"},
		"invalid email":  {"username": "testuser", "email": "not-an-email", "password": "password123"},
	}

	for name, body := range tests {
		t.Run(name, func(t *testing.T) {
			statusCode, respBody := testutil.RegisterUser(t, server, body["username"], body["email"], body["password"])
			assert.Equal(t, http.StatusBadRequest, statusCode)
			code, _, _ := testutil.ParseResponse(t, respBody)
			assert.Equal(t, 40000, code)
		})
	}
}

// TestLogin_Success — login with correct credentials, expect HttpOnly cookies and role
func TestLogin_Success(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	// Register first
	statusCode, respBody := testutil.RegisterUser(t, server, "loginuser", "loginuser@example.com", "password123")
	require.Equal(t, http.StatusOK, statusCode)
	code, _, _ := testutil.ParseResponse(t, respBody)
	require.Equal(t, 0, code)

	// H1: Login returns cookies instead of JSON token
	statusCode, respBody, setCookies := testutil.LoginUserWithCookies(t, server, "loginuser", "password123")
	assert.Equal(t, http.StatusOK, statusCode)
	code, _, data := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)
	assert.NotNil(t, data)
	assert.Contains(t, data, "device_id")

	// First user should be admin
	assert.Equal(t, "admin", data["role"], "first user should have role=admin")

	// H1: Verify HttpOnly cookies are set
	token := testutil.ExtractCookie(setCookies, "device_token")
	refreshToken := testutil.ExtractCookie(setCookies, "refresh_token")
	deviceID, _ := data["device_id"].(string)
	assert.NotEmpty(t, token, "device_token cookie should be set")
	assert.NotEmpty(t, refreshToken, "refresh_token cookie should be set")
	assert.NotEmpty(t, deviceID, "device_id should be in response body")
}

// TestLogin_WrongPassword — login with wrong password, expect code=40101
func TestLogin_WrongPassword(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	// Register first
	statusCode, respBody := testutil.RegisterUser(t, server, "wrongpw", "wrongpw@example.com", "correctpassword")
	require.Equal(t, http.StatusOK, statusCode)

	// Login with wrong password
	statusCode, respBody = testutil.LoginUser(t, server, "wrongpw", "wrongpassword")
	assert.Equal(t, http.StatusUnauthorized, statusCode)
	code, message, _ := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 40101, code)
	assert.Contains(t, message, "用户名或密码错误")
}

// TestLogin_RateLimit_IP — 5 failed logins from same IP, expect code=42901
func TestLogin_RateLimit_IP(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	testIP := "10.0.0.1"

	// 5 failed logins from same IP
	for i := 0; i < 5; i++ {
		statusCode, respBody := testutil.PostWithIP(t, server, "/api/v1/auth/login", testIP, map[string]string{
			"username": "nonexistent_user",
			"password": "wrongpassword",
		})
		// All 5 should return 401 (wrong creds), the 5th triggers the ban
		_ = respBody
		if i < 4 {
			assert.Equal(t, http.StatusUnauthorized, statusCode, "attempt %d should return 401", i+1)
		}
	}

	// 6th attempt should be rate limited
	statusCode, respBody := testutil.PostWithIP(t, server, "/api/v1/auth/login", testIP, map[string]string{
		"username": "nonexistent_user",
		"password": "wrongpassword",
	})
	assert.Equal(t, http.StatusTooManyRequests, statusCode)
	code, message, _ := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 42901, code)
	assert.Contains(t, message, "尝试次数过多")
}

// TestLogin_RateLimit_User — 10 failed logins for same user (different IPs), expect code=42301
func TestLogin_RateLimit_User(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	// Register user first
	statusCode, respBody := testutil.RegisterUser(t, server, "ratelimituser", "ratelimit@example.com", "correctpassword")
	require.Equal(t, http.StatusOK, statusCode)
	code, _, _ := testutil.ParseResponse(t, respBody)
	require.Equal(t, 0, code)

	// 10 failed logins from different IPs
	for i := 0; i < 10; i++ {
		ip := fmt.Sprintf("10.0.1.%d", i+1)
		statusCode, _ := testutil.PostWithIP(t, server, "/api/v1/auth/login", ip, map[string]string{
			"username": "ratelimituser",
			"password": "wrongpassword",
		})
		_ = statusCode
	}

	// 11th attempt should get user locked response (423)
	statusCode, respBody = testutil.PostWithIP(t, server, "/api/v1/auth/login", "10.0.1.99", map[string]string{
		"username": "ratelimituser",
		"password": "wrongpassword",
	})
	assert.Equal(t, http.StatusLocked, statusCode)
	code, message, _ := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 42301, code)
	assert.Contains(t, message, "账号已锁定")
}

// TestAuth_ExpiredToken — use expired JWT token, expect code=40102
func TestAuth_ExpiredToken(t *testing.T) {
	// Use a server with a short-lived token config
	server, cfg := testutil.SetupTestServerWithShortToken(t)

	// We need to generate an expired token directly since the normal login
	// creates a 30-day token. We'll register a user, then create our own
	// expired token using the same secret.

	// First register and login to get a valid token structure
	testutil.RegisterUser(t, server, "expireuser", "expireuser@example.com", "password123")

	// Look up the actual user ID from the database
	var dbUser model.User
	err := testutil.TestDB.Where("username = ?", "expireuser").First(&dbUser).Error
	require.NoError(t, err)

	// Generate an expired token manually using the same JWT secret
	// We need to create a token with "exp" in the past
	token := testutil.GenerateExpiredToken(t, cfg.JWTSecret, dbUser.ID)

	// Try to access a protected endpoint with the expired token
	statusCode, respBody := testutil.AuthGet(t, server, "/api/v1/devices", token)
	assert.Equal(t, http.StatusUnauthorized, statusCode)
	code, message, _ := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 40102, code)
	assert.Contains(t, message, "无效")
}

func TestAuth_Refresh_Success(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	testutil.RegisterUser(t, server, "refreshuser", "refreshuser@example.com", "password123")
	_, _, setCookies := testutil.LoginUserWithCookies(t, server, "refreshuser", "password123")
	deviceToken := testutil.ExtractCookie(setCookies, "device_token")
	require.NotEmpty(t, deviceToken)

	cookies := fmt.Sprintf("device_token=%s", deviceToken)
	statusCode, respBody := testutil.RefreshWithCookies(t, server, cookies)
	assert.Equal(t, http.StatusOK, statusCode)
	code, message, _ := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)
	assert.Equal(t, "Token 刷新成功", message)
}

func TestAuth_Logout_Success(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	testutil.RegisterUser(t, server, "logoutuser", "logoutuser@example.com", "password123")
	_, _, setCookies := testutil.LoginUserWithCookies(t, server, "logoutuser", "password123")

	deviceToken := testutil.ExtractCookie(setCookies, "device_token")
	require.NotEmpty(t, deviceToken)

	cookies := fmt.Sprintf("device_token=%s", deviceToken)
	statusCode, respBody := testutil.LogoutWithCookies(t, server, cookies)
	assert.Equal(t, http.StatusOK, statusCode)
	code, message, _ := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)
	assert.Equal(t, "已退出登录", message)

	loginRespCode, _ := testutil.AuthGet(t, server, "/api/v1/devices", deviceToken)
	assert.Equal(t, http.StatusUnauthorized, loginRespCode)
}
