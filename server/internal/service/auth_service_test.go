package service

import (
	"os"
	"testing"
	"time"

	"ditto-cloud-server/internal/config"
	"ditto-cloud-server/internal/database"
	"ditto-cloud-server/internal/model"

	"github.com/golang-jwt/jwt/v5"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// setupAuthServiceTest creates an isolated test environment for AuthService tests
func setupAuthServiceTest(t *testing.T) (*AuthService, func()) {
	t.Helper()

	// Create temp database file
	tmpFile, err := os.CreateTemp("", "auth_service_test_*.db")
	require.NoError(t, err)
	dbPath := tmpFile.Name()
	tmpFile.Close()

	// Initialize database
	err = database.Init(dbPath)
	require.NoError(t, err)

	// Create config
	cfg := &config.Config{
		Port:         "0",
		DatabasePath: dbPath,
		JWTSecret:    "test-jwt-secret-key",
		StartTime:    time.Now(),
	}

	// Create service
	svc := NewAuthService(cfg)

	cleanup := func() {
		database.DB = nil
		os.Remove(dbPath)
		os.Remove(dbPath + "-shm")
		os.Remove(dbPath + "-wal")
	}

	return svc, cleanup
}

func TestNewAuthService(t *testing.T) {
	cfg := &config.Config{
		JWTSecret: "test-secret",
	}
	svc := NewAuthService(cfg)
	assert.NotNil(t, svc)
	assert.Equal(t, cfg, svc.cfg)
}

func TestAuthService_Register_Success(t *testing.T) {
	svc, cleanup := setupAuthServiceTest(t)
	defer cleanup()

	req := &RegisterRequest{
		Username: "testuser",
		Email:    "test@example.com",
		Password: "password123",
	}

	resp, err := svc.Register(req)

	assert.NoError(t, err)
	assert.NotNil(t, resp)
	assert.Greater(t, resp.UserID, uint(0))

	// Verify user was created in database
	var user model.User
	err = database.DB.First(&user, resp.UserID).Error
	assert.NoError(t, err)
	assert.Equal(t, "testuser", user.Username)
	assert.Equal(t, "test@example.com", user.Email)
	assert.NotEmpty(t, user.PasswordHash)
}

func TestAuthService_Register_DuplicateUsername(t *testing.T) {
	svc, cleanup := setupAuthServiceTest(t)
	defer cleanup()

	// First registration
	req1 := &RegisterRequest{
		Username: "testuser",
		Email:    "test1@example.com",
		Password: "password123",
	}
	_, err := svc.Register(req1)
	require.NoError(t, err)

	// Second registration with same username
	req2 := &RegisterRequest{
		Username: "testuser",
		Email:    "test2@example.com",
		Password: "password456",
	}
	_, err = svc.Register(req2)

	assert.Error(t, err)
	assert.Equal(t, ErrUsernameExists, err)
}

func TestAuthService_Register_DuplicateEmail(t *testing.T) {
	svc, cleanup := setupAuthServiceTest(t)
	defer cleanup()

	// First registration
	req1 := &RegisterRequest{
		Username: "user1",
		Email:    "same@example.com",
		Password: "password123",
	}
	_, err := svc.Register(req1)
	require.NoError(t, err)

	// Second registration with same email
	req2 := &RegisterRequest{
		Username: "user2",
		Email:    "same@example.com",
		Password: "password456",
	}
	_, err = svc.Register(req2)

	assert.Error(t, err)
	assert.Equal(t, ErrEmailExists, err)
}

func TestAuthService_Login_Success(t *testing.T) {
	svc, cleanup := setupAuthServiceTest(t)
	defer cleanup()

	// Register user first
	regReq := &RegisterRequest{
		Username: "loginuser",
		Email:    "login@example.com",
		Password: "correctpassword",
	}
	_, err := svc.Register(regReq)
	require.NoError(t, err)

	// Login
	loginReq := &LoginRequest{
		Username: "loginuser",
		Password: "correctpassword",
	}
	resp, err := svc.Login(loginReq, "test-device")

	assert.NoError(t, err)
	assert.NotNil(t, resp)
	assert.NotEmpty(t, resp.DeviceToken)
	assert.NotEmpty(t, resp.DeviceID)
	assert.Contains(t, resp.DeviceID, "dev-")
}

func TestAuthService_Login_WrongPassword(t *testing.T) {
	svc, cleanup := setupAuthServiceTest(t)
	defer cleanup()

	// Register user first
	regReq := &RegisterRequest{
		Username: "wrongpwuser",
		Email:    "wrongpw@example.com",
		Password: "correctpassword",
	}
	_, err := svc.Register(regReq)
	require.NoError(t, err)

	// Login with wrong password
	loginReq := &LoginRequest{
		Username: "wrongpwuser",
		Password: "wrongpassword",
	}
	resp, err := svc.Login(loginReq, "test-device")

	assert.Error(t, err)
	assert.Equal(t, ErrInvalidCreds, err)
	assert.Nil(t, resp)
}

func TestAuthService_Login_UserNotFound(t *testing.T) {
	svc, cleanup := setupAuthServiceTest(t)
	defer cleanup()

	loginReq := &LoginRequest{
		Username: "nonexistent",
		Password: "password",
	}
	resp, err := svc.Login(loginReq, "test-device")

	assert.Error(t, err)
	assert.Equal(t, ErrInvalidCreds, err)
	assert.Nil(t, resp)
}

func TestAuthService_Login_MultipleDevices(t *testing.T) {
	svc, cleanup := setupAuthServiceTest(t)
	defer cleanup()

	// Register user
	regReq := &RegisterRequest{
		Username: "multidevice",
		Email:    "multi@example.com",
		Password: "password",
	}
	_, err := svc.Register(regReq)
	require.NoError(t, err)

	// Login from first device
	resp1, err := svc.Login(&LoginRequest{
		Username: "multidevice",
		Password: "password",
	}, "device1")
	require.NoError(t, err)

	// Login from second device
	resp2, err := svc.Login(&LoginRequest{
		Username: "multidevice",
		Password: "password",
	}, "device2")
	require.NoError(t, err)

	// Different device IDs
	assert.NotEqual(t, resp1.DeviceID, resp2.DeviceID)

	// Verify two devices exist in database
	var count int64
	database.DB.Model(&model.Device{}).Where("user_id = ?", 1).Count(&count)
	assert.Equal(t, int64(2), count)
}

func TestAuthService_RefreshDeviceToken_Success(t *testing.T) {
	svc, cleanup := setupAuthServiceTest(t)
	defer cleanup()

	// Register and login
	regReq := &RegisterRequest{
		Username: "refreshuser",
		Email:    "refresh@example.com",
		Password: "password",
	}
	_, err := svc.Register(regReq)
	require.NoError(t, err)

	loginReq := &LoginRequest{
		Username: "refreshuser",
		Password: "password",
	}
	loginResp, err := svc.Login(loginReq, "test-device")
	require.NoError(t, err)

	// Refresh token
	newToken, _, err := svc.RefreshDeviceToken(loginResp.DeviceID, loginResp.DeviceToken)

	assert.NoError(t, err)
	assert.NotEmpty(t, newToken)

	// Verify new token is valid
	token, err := jwt.Parse(newToken, func(token *jwt.Token) (interface{}, error) {
		return []byte(svc.cfg.JWTSecret), nil
	})
	assert.NoError(t, err)
	assert.True(t, token.Valid)

	// Verify claims
	claims := token.Claims.(jwt.MapClaims)
	assert.Equal(t, float64(1), claims["user_id"])
	assert.Equal(t, loginResp.DeviceID, claims["device_id"])
}

func TestAuthService_RefreshDeviceToken_InvalidToken(t *testing.T) {
	svc, cleanup := setupAuthServiceTest(t)
	defer cleanup()

	_, _, err := svc.RefreshDeviceToken("dev-1-test", "invalid-token-string")

	assert.Error(t, err)
	assert.Contains(t, err.Error(), "无效")
}

func TestAuthService_RefreshDeviceToken_DeviceMismatch(t *testing.T) {
	svc, cleanup := setupAuthServiceTest(t)
	defer cleanup()

	// Register and login
	regReq := &RegisterRequest{
		Username: "mismatchuser",
		Email:    "mismatch@example.com",
		Password: "password",
	}
	_, err := svc.Register(regReq)
	require.NoError(t, err)

	loginResp, err := svc.Login(&LoginRequest{
		Username: "mismatchuser",
		Password: "password",
	}, "device1")
	require.NoError(t, err)

	// Try to refresh with different device ID
	_, _, err = svc.RefreshDeviceToken("different-device-id", loginResp.DeviceToken)

	assert.Error(t, err)
	assert.Contains(t, err.Error(), "设备不匹配")
}

func TestAuthService_RefreshDeviceToken_UserDeleted(t *testing.T) {
	svc, cleanup := setupAuthServiceTest(t)
	defer cleanup()

	// Register and login
	regReq := &RegisterRequest{
		Username: "deleteduser",
		Email:    "deleted@example.com",
		Password: "password",
	}
	_, err := svc.Register(regReq)
	require.NoError(t, err)

	loginResp, err := svc.Login(&LoginRequest{
		Username: "deleteduser",
		Password: "password",
	}, "device1")
	require.NoError(t, err)

	// Delete the user's devices first (due to foreign key constraints)
	database.DB.Where("user_id = ?", 1).Delete(&model.Device{})
	// Then delete the user
	database.DB.Where("username = ?", "deleteduser").Delete(&model.User{})

	// Try to refresh token - should fail because user no longer exists
	_, _, err = svc.RefreshDeviceToken(loginResp.DeviceID, loginResp.DeviceToken)

	assert.Error(t, err)
	assert.Contains(t, err.Error(), "用户不存在")
}

func TestAuthService_GenerateToken(t *testing.T) {
	svc, cleanup := setupAuthServiceTest(t)
	defer cleanup()

	userID := uint(123)
	deviceID := "test-device-123"

	token, err := svc.generateToken(userID, deviceID, 24*time.Hour)

	assert.NoError(t, err)
	assert.NotEmpty(t, token)

	// Parse and verify claims
	parsed, err := jwt.Parse(token, func(token *jwt.Token) (interface{}, error) {
		return []byte(svc.cfg.JWTSecret), nil
	})
	require.NoError(t, err)

	claims := parsed.Claims.(jwt.MapClaims)
	assert.Equal(t, float64(userID), claims["user_id"])
	assert.Equal(t, deviceID, claims["device_id"])
	assert.NotNil(t, claims["exp"])
	assert.NotNil(t, claims["iat"])
}

func TestGenerateDeviceID(t *testing.T) {
	userID := uint(42)
	deviceName := "my-laptop"

	deviceID := generateDeviceID(userID, deviceName)

	assert.Equal(t, "dev-42-my-laptop", deviceID)
}

func TestAuthService_ErrorConstants(t *testing.T) {
	assert.Equal(t, "用户名已存在", ErrUsernameExists.Error())
	assert.Equal(t, "邮箱已被注册", ErrEmailExists.Error())
	assert.Equal(t, "用户名或密码错误", ErrInvalidCreds.Error())
	assert.Equal(t, "账号已锁定", ErrUserLocked.Error())
	assert.Equal(t, "尝试次数过多", ErrTooManyAttempts.Error())
}
