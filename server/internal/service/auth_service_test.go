package service

import (
	"os"
	"testing"
	"time"

	"ditto-cloud-server/internal/config"
	"ditto-cloud-server/internal/database"
	"ditto-cloud-server/internal/model"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

func setupAuthServiceTest(t *testing.T) (*AuthService, uint, string, func()) {
	t.Helper()

	tmpFile, err := os.CreateTemp("", "auth_service_test_*.db")
	require.NoError(t, err)
	dbPath := tmpFile.Name()
	tmpFile.Close()

	err = database.Init(dbPath)
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

func TestAuthService_GenerateDeviceID(t *testing.T) {
	deviceID := generateDeviceID(1, "MyDevice")

	assert.Equal(t, "dev-1-MyDevice", deviceID)
}