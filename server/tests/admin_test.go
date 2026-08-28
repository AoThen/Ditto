package tests

import (
	"fmt"
	"net/http"
	"testing"

	"ditto-cloud-server/tests/testutil"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// TestRegister_FirstUserIsAdmin — first registered user gets role=admin
func TestRegister_FirstUserIsAdmin(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	statusCode, respBody := testutil.RegisterUser(t, server, "firstadmin", "firstadmin@example.com", "password123")
	require.Equal(t, http.StatusOK, statusCode)

	// Login to verify role
	statusCode, respBody, _ = testutil.LoginUserWithCookies(t, server, "firstadmin", "password123")
	assert.Equal(t, http.StatusOK, statusCode)
	_, _, data := testutil.ParseResponse(t, respBody)
	assert.Equal(t, "admin", data["role"])
}

// TestRegister_ClosedAfterFirstUser — second registration attempt is rejected
func TestRegister_ClosedAfterFirstUser(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	// First registration succeeds
	statusCode, _ := testutil.RegisterUser(t, server, "user1", "user1@example.com", "password123")
	assert.Equal(t, http.StatusOK, statusCode)

	// Second registration is rejected
	statusCode, respBody := testutil.RegisterUser(t, server, "user2", "user2@example.com", "password456")
	assert.Equal(t, http.StatusForbidden, statusCode)
	code, message, _ := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 40302, code)
	assert.Contains(t, message, "注册已关闭")
}

// TestAdmin_CreateUser — admin creates a regular user, expect success
func TestAdmin_CreateUser(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	token, _ := testutil.RegisterAndLogin(t, server, "admin1", "admin1@example.com", "adminpass123")

	respCode, respBody := testutil.AuthPost(t, server, "/api/v1/admin/users", token, map[string]string{
		"username": "regularuser",
		"email":    "regular@example.com",
		"password": "userpass123",
	})
	assert.Equal(t, http.StatusOK, respCode)
	code, message, data := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)
	assert.Contains(t, message, "创建成功")
	assert.NotNil(t, data)
	assert.Contains(t, data, "user_id")
}

// TestAdmin_CreateUser_DuplicateUsername — admin creates user with existing username
func TestAdmin_CreateUser_DuplicateUsername(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	token, _ := testutil.RegisterAndLogin(t, server, "admin2", "admin2@example.com", "adminpass123")

	testutil.AuthPost(t, server, "/api/v1/admin/users", token, map[string]string{
		"username": "dupuser",
		"email":    "dup1@example.com",
		"password": "pass123",
	})

	respCode, respBody := testutil.AuthPost(t, server, "/api/v1/admin/users", token, map[string]string{
		"username": "dupuser",
		"email":    "dup2@example.com",
		"password": "pass456",
	})
	assert.Equal(t, http.StatusBadRequest, respCode)
	code, message, _ := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 40001, code)
	assert.Contains(t, message, "用户名已存在")
}

// TestAdmin_CreateUser_Unauthorized — regular user tries to access admin API
func TestAdmin_CreateUser_Unauthorized(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	// Register first user (admin)
	token, _ := testutil.RegisterAndLogin(t, server, "admin3", "admin3@example.com", "adminpass123")

	// Create a regular user via admin
	testutil.AuthPost(t, server, "/api/v1/admin/users", token, map[string]string{
		"username": "regular",
		"email":    "regular@example.com",
		"password": "userpass123",
	})

	// Login as regular user and get token from cookie
	_, _, respCookies := testutil.LoginUserWithCookies(t, server, "regular", "userpass123")
	regToken := testutil.ExtractCookie(respCookies, "device_token")
	require.NotEmpty(t, regToken, "regular user should have a token")

	// Regular user tries admin API
	respCode, respBody := testutil.AuthPost(t, server, "/api/v1/admin/users", regToken, map[string]string{
		"username": "another",
		"email":    "another@example.com",
		"password": "pass123",
	})
	assert.Equal(t, http.StatusForbidden, respCode)
	code, message, _ := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 40301, code)
	assert.Contains(t, message, "需要管理员权限")
}

// TestAdmin_ListUsers — admin lists all users
func TestAdmin_ListUsers(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	token, _ := testutil.RegisterAndLogin(t, server, "admin4", "admin4@example.com", "adminpass123")

	for _, u := range []string{"user_a", "user_b", "user_c"} {
		testutil.AuthPost(t, server, "/api/v1/admin/users", token, map[string]string{
			"username": u,
			"email":    u + "@example.com",
			"password": "pass123",
		})
	}

	respCode, respBody := testutil.AuthGet(t, server, "/api/v1/admin/users?page=1&per_page=10", token)
	assert.Equal(t, http.StatusOK, respCode)
	code, _, data := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)
	assert.NotNil(t, data)
	assert.Contains(t, data, "items")
	assert.Contains(t, data, "total")
	total, ok := data["total"].(float64)
	assert.True(t, ok)
	assert.Equal(t, float64(4), total) // admin + 3 users
}

// TestAdmin_DeleteUser — admin deletes a regular user
func TestAdmin_DeleteUser(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	token, _ := testutil.RegisterAndLogin(t, server, "admin5", "admin5@example.com", "adminpass123")

	respCode, respBody := testutil.AuthPost(t, server, "/api/v1/admin/users", token, map[string]string{
		"username": "todelete",
		"email":    "todelete@example.com",
		"password": "pass123",
	})
	require.Equal(t, http.StatusOK, respCode)
	_, _, data := testutil.ParseResponse(t, respBody)
	userID := fmt.Sprintf("%.0f", data["user_id"].(float64))

	respCode, respBody = testutil.AuthDelete(t, server, "/api/v1/admin/users/"+userID, token)
	assert.Equal(t, http.StatusOK, respCode)
	code, message, _ := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)
	assert.Contains(t, message, "已删除")
}

// TestAdmin_DeleteUser_LastAdmin — cannot delete last admin
func TestAdmin_DeleteUser_LastAdmin(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	token, _ := testutil.RegisterAndLogin(t, server, "onlyadmin", "onlyadmin@example.com", "adminpass123")

	respCode, respBody := testutil.AuthDelete(t, server, "/api/v1/admin/users/1", token)
	assert.Equal(t, http.StatusBadRequest, respCode)
	code, message, _ := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 40003, code)
	assert.Contains(t, message, "无法删除最后一个管理员账号")
}

// TestAdmin_ResetPassword — admin resets a user's password
func TestAdmin_ResetPassword(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	token, _ := testutil.RegisterAndLogin(t, server, "admin6", "admin6@example.com", "adminpass123")

	respCode, respBody := testutil.AuthPost(t, server, "/api/v1/admin/users", token, map[string]string{
		"username": "resetme",
		"email":    "resetme@example.com",
		"password": "oldpass123",
	})
	require.Equal(t, http.StatusOK, respCode)
	_, _, data := testutil.ParseResponse(t, respBody)
	userID := fmt.Sprintf("%.0f", data["user_id"].(float64))

	respCode, respBody = testutil.AuthPost(t, server, "/api/v1/admin/users/"+userID+"/reset-password", token, map[string]string{
		"password": "newpass456",
	})
	assert.Equal(t, http.StatusOK, respCode)
	code, message, _ := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)
	assert.Contains(t, message, "密码已重置")

	statusCode, _ := testutil.LoginUser(t, server, "resetme", "newpass456")
	assert.Equal(t, http.StatusOK, statusCode)
}

func TestAdmin_GetUser(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	token, _ := testutil.RegisterAndLogin(t, server, "admin7", "admin7@example.com", "adminpass123")

	_, respBody := testutil.AuthPost(t, server, "/api/v1/admin/users", token, map[string]string{
		"username": "getuser",
		"email":    "getuser@example.com",
		"password": "pass123",
	})
	_, _, data := testutil.ParseResponse(t, respBody)
	userID := fmt.Sprintf("%.0f", data["user_id"].(float64))

	respCode, respBody := testutil.AuthGet(t, server, "/api/v1/admin/users/"+userID, token)
	assert.Equal(t, http.StatusOK, respCode)
	respCode, _, respData := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, respCode)
	assert.Equal(t, "getuser", respData["username"])
	assert.Equal(t, "getuser@example.com", respData["email"])
}

func TestAdmin_GetUser_NotFound(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	token, _ := testutil.RegisterAndLogin(t, server, "admin8", "admin8@example.com", "adminpass123")

	respCode, _ := testutil.AuthGet(t, server, "/api/v1/admin/users/99999", token)
	assert.Equal(t, http.StatusNotFound, respCode)
}

func TestAdmin_UpdateUser(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	token, _ := testutil.RegisterAndLogin(t, server, "admin9", "admin9@example.com", "adminpass123")

	_, respBody := testutil.AuthPost(t, server, "/api/v1/admin/users", token, map[string]string{
		"username": "updateme",
		"email":    "updateme@example.com",
		"password": "pass123",
	})
	_, _, data := testutil.ParseResponse(t, respBody)
	userID := fmt.Sprintf("%.0f", data["user_id"].(float64))

	respCode, respBody := testutil.AuthPut(t, server, "/api/v1/admin/users/"+userID, token, map[string]string{
		"role": "user",
	})
	assert.Equal(t, http.StatusOK, respCode)
	respCode, message, _ := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, respCode)
	assert.Equal(t, "用户更新成功", message)

	respCode, respBody = testutil.AuthGet(t, server, "/api/v1/admin/users/"+userID, token)
	assert.Equal(t, http.StatusOK, respCode)
	_, _, respData := testutil.ParseResponse(t, respBody)
	assert.Equal(t, "user", respData["role"])
}
