package service

import (
	"os"
	"testing"
	"time"

	"ditto-cloud-server/internal/config"
	"ditto-cloud-server/internal/database"
	"ditto-cloud-server/internal/model"
	"ditto-cloud-server/pkg/crypto"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

func setupAuthServiceTest(t *testing.T) (*AuthService, uint, string, func()) {
	t.Helper()

	tmpFile, err := os.CreateTemp("", "auth_service_test_*.db")
	require.NoError(t, err)
	dbPath := tmpFile.Name()
	tmpFile.Close()

	err = database.Init(dbPath, 500*time.Millisecond)
	require.NoError(t, err)

	cfg := &config.Config{
		JWTSecret:          "test-secret-key-for-testing",
		TokenExpiryAccess:  30 * 24 * time.Hour,
		TokenExpiryRefresh: 90 * 24 * time.Hour,
	}

	svc := NewAuthService(cfg)

	user := model.User{
		Username:     "authsvcuser",
		Email:        "authsvc@example.com",
		PasswordHash: "hash",
	}
	err = database.DB.Create(&user).Error
	require.NoError(t, err)

	device := model.Device{
		ID:         "auth-svc-device",
		UserID:     user.ID,
		DeviceName: "Auth Test Device",
	}
	err = database.DB.Create(&device).Error
	require.NoError(t, err)

	cleanup := func() {
		database.DB = nil
		os.Remove(dbPath)
		os.Remove(dbPath + "-shm")
		os.Remove(dbPath + "-wal")
	}

	return svc, user.ID, device.ID, cleanup
}

func TestAuthService_GetTokenExpiryAccess(t *testing.T) {
	svc := NewAuthService(&config.Config{TokenExpiryAccess: 24 * time.Hour})

	expiry := svc.GetTokenExpiryAccess()

	assert.Equal(t, 24*time.Hour, expiry)
}

func TestAuthService_GetTokenExpiryRefresh(t *testing.T) {
	svc := NewAuthService(&config.Config{TokenExpiryRefresh: 7 * 24 * time.Hour})

	expiry := svc.GetTokenExpiryRefresh()

	assert.Equal(t, 7*24*time.Hour, expiry)
}

func TestAuthService_IsCookieSecure(t *testing.T) {
	svc := NewAuthService(&config.Config{CookieSecure: true})

	assert.True(t, svc.IsCookieSecure())
}

func TestAuthService_RefreshDeviceToken_Success(t *testing.T) {
	svc, userID, deviceID, cleanup := setupAuthServiceTest(t)
	defer cleanup()

	accessToken, refreshToken, err := svc.RefreshDeviceToken(userID, deviceID)

	assert.NoError(t, err)
	assert.NotEmpty(t, accessToken)
	assert.NotEmpty(t, refreshToken)
}

func TestAuthService_RefreshDeviceToken_UserNotFound(t *testing.T) {
	svc, _, deviceID, cleanup := setupAuthServiceTest(t)
	defer cleanup()

	_, _, err := svc.RefreshDeviceToken(99999, deviceID)

	assert.Error(t, err)
	assert.Equal(t, "用户不存在", err.Error())
}

func TestAuthService_RefreshDeviceToken_DeviceNotFound(t *testing.T) {
	svc, userID, _, cleanup := setupAuthServiceTest(t)
	defer cleanup()

	_, _, err := svc.RefreshDeviceToken(userID, "non-existent-device")

	assert.Error(t, err)
	assert.Equal(t, "设备不存在", err.Error())
}

func setupLoginTest(t *testing.T, password string) (*AuthService, uint, func()) {
	t.Helper()

	tmpFile, err := os.CreateTemp("", "auth_login_test_*.db")
	require.NoError(t, err)
	dbPath := tmpFile.Name()
	tmpFile.Close()

	err = database.Init(dbPath, 500*time.Millisecond)
	require.NoError(t, err)

	cfg := &config.Config{
		JWTSecret:          "test-secret-key-for-testing",
		TokenExpiryAccess:  30 * 24 * time.Hour,
		TokenExpiryRefresh: 90 * 24 * time.Hour,
	}
	svc := NewAuthService(cfg)

	hashedPassword, err := crypto.HashPassword(password)
	require.NoError(t, err)

	user := model.User{
		Username:     "loginuser",
		Email:        "login@example.com",
		PasswordHash: hashedPassword,
		IsActive:     true,
	}
	err = database.DB.Create(&user).Error
	require.NoError(t, err)

	cleanup := func() {
		database.DB = nil
		os.Remove(dbPath)
		os.Remove(dbPath + "-shm")
		os.Remove(dbPath + "-wal")
	}

	return svc, user.ID, cleanup
}

func TestAuthService_Register(t *testing.T) {
	t.Run("Success", func(t *testing.T) {
		svc, _, _, cleanup := setupAuthServiceTest(t)
		defer cleanup()

		req := &RegisterRequest{
			Username: "newuser",
			Email:    "new@example.com",
			Password: "password123",
		}
		resp, err := svc.Register(req)
		assert.NoError(t, err)
		assert.NotZero(t, resp.UserID)
	})

	t.Run("DuplicateUsername", func(t *testing.T) {
		svc, _, _, cleanup := setupAuthServiceTest(t)
		defer cleanup()

		req := &RegisterRequest{
			Username: "authsvcuser",
			Email:    "another@example.com",
			Password: "password123",
		}
		_, err := svc.Register(req)
		assert.Error(t, err)
		assert.Equal(t, ErrUsernameExists, err)
	})

	t.Run("DuplicateEmail", func(t *testing.T) {
		svc, _, _, cleanup := setupAuthServiceTest(t)
		defer cleanup()

		req := &RegisterRequest{
			Username: "anotheruser",
			Email:    "authsvc@example.com",
			Password: "password123",
		}
		_, err := svc.Register(req)
		assert.Error(t, err)
		assert.Equal(t, ErrEmailExists, err)
	})

	t.Run("FirstUserBecomesAdmin", func(t *testing.T) {
		tmpFile, err := os.CreateTemp("", "auth_admin_*.db")
		require.NoError(t, err)
		dbPath := tmpFile.Name()
		tmpFile.Close()
		defer os.Remove(dbPath)
		defer os.Remove(dbPath + "-shm")
		defer os.Remove(dbPath + "-wal")

		err = database.Init(dbPath, 500*time.Millisecond)
		require.NoError(t, err)
		defer func() { database.DB = nil }()

		cfg := &config.Config{JWTSecret: "test"}
		svc := NewAuthService(cfg)

		req := &RegisterRequest{
			Username: "firstuser",
			Email:    "first@example.com",
			Password: "password123",
		}
		resp, err := svc.Register(req)
		require.NoError(t, err)

		var user model.User
		err = database.DB.Where("id = ?", resp.UserID).First(&user).Error
		require.NoError(t, err)
		assert.Equal(t, "admin", user.Role)
	})
}

func TestAuthService_Login(t *testing.T) {
	t.Run("Success", func(t *testing.T) {
		password := "testpassword123"
		svc, userID, cleanup := setupLoginTest(t, password)
		defer cleanup()

		req := &LoginRequest{Username: "loginuser", Password: password}
		resp, err := svc.Login(req, "test-device")
		assert.NoError(t, err)
		assert.Equal(t, userID, resp.UserID)
		assert.NotEmpty(t, resp.DeviceToken)
		assert.NotEmpty(t, resp.DeviceID)
		assert.Equal(t, "user", resp.Role)
	})

	t.Run("WrongPassword", func(t *testing.T) {
		svc, _, cleanup := setupLoginTest(t, "correctpassword")
		defer cleanup()

		req := &LoginRequest{Username: "loginuser", Password: "wrongpassword"}
		_, err := svc.Login(req, "test-device")
		assert.Error(t, err)
		assert.Equal(t, ErrInvalidCreds, err)
	})

	t.Run("UserNotFound", func(t *testing.T) {
		svc, _, cleanup := setupLoginTest(t, "password123")
		defer cleanup()

		req := &LoginRequest{Username: "nonexistent", Password: "password123"}
		_, err := svc.Login(req, "test-device")
		assert.Error(t, err)
		assert.Equal(t, ErrInvalidCreds, err)
	})
}

func TestAuthService_IsFirstUser(t *testing.T) {
	t.Run("True", func(t *testing.T) {
		tmpFile, err := os.CreateTemp("", "auth_isfirst_true_*.db")
		require.NoError(t, err)
		dbPath := tmpFile.Name()
		tmpFile.Close()
		defer os.Remove(dbPath)
		defer os.Remove(dbPath + "-shm")
		defer os.Remove(dbPath + "-wal")

		err = database.Init(dbPath, 500*time.Millisecond)
		require.NoError(t, err)
		defer func() { database.DB = nil }()

		assert.True(t, IsFirstUser())
	})

	t.Run("False", func(t *testing.T) {
		_, _, _, cleanup := setupAuthServiceTest(t)
		defer cleanup()

		assert.False(t, IsFirstUser())
	})
}

func TestAuthService_RegisterAllowed(t *testing.T) {
	t.Run("True", func(t *testing.T) {
		tmpFile, err := os.CreateTemp("", "auth_regallowed_true_*.db")
		require.NoError(t, err)
		dbPath := tmpFile.Name()
		tmpFile.Close()
		defer os.Remove(dbPath)
		defer os.Remove(dbPath + "-shm")
		defer os.Remove(dbPath + "-wal")

		err = database.Init(dbPath, 500*time.Millisecond)
		require.NoError(t, err)
		defer func() { database.DB = nil }()

		assert.True(t, RegisterAllowed())
	})

	t.Run("False", func(t *testing.T) {
		_, _, _, cleanup := setupAuthServiceTest(t)
		defer cleanup()

		assert.False(t, RegisterAllowed())
	})
}

func TestAuthService_ResetPassword(t *testing.T) {
	t.Run("Success", func(t *testing.T) {
		svc, _, cleanup := setupLoginTest(t, "oldpassword")
		defer cleanup()

		err := ResetPasswordByUsername("loginuser", "newpassword")
		require.NoError(t, err)

		req := &LoginRequest{Username: "loginuser", Password: "newpassword"}
		resp, err := svc.Login(req, "test-device")
		assert.NoError(t, err)
		assert.NotEmpty(t, resp.DeviceToken)
	})

	t.Run("UserNotFound", func(t *testing.T) {
		_, _, cleanup := setupLoginTest(t, "password123")
		defer cleanup()

		err := ResetPasswordByUsername("nonexistent", "newpassword")
		assert.Error(t, err)
		assert.Equal(t, "用户不存在", err.Error())
	})
}

func TestAuthService_GenerateDeviceID(t *testing.T) {
	deviceID := generateDeviceID(1, "MyDevice")

	assert.Equal(t, "dev-1-MyDevice", deviceID)
}