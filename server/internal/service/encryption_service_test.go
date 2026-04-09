package service

import (
	"encoding/base64"
	"os"
	"testing"

	"ditto-cloud-server/internal/database"
	"ditto-cloud-server/internal/model"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// setupEncryptionServiceTest creates an isolated test environment for EncryptionService tests
func setupEncryptionServiceTest(t *testing.T) (*EncryptionService, uint, func()) {
	t.Helper()

	// Create temp database file
	tmpFile, err := os.CreateTemp("", "encryption_service_test_*.db")
	require.NoError(t, err)
	dbPath := tmpFile.Name()
	tmpFile.Close()

	// Initialize database
	err = database.Init(dbPath)
	require.NoError(t, err)

	// Create test user
	user := model.User{
		Username:     "testuser",
		Email:        "test@example.com",
		PasswordHash: "hashedpassword",
	}
	require.NoError(t, database.DB.Create(&user).Error)

	// Create service
	svc := NewEncryptionService()

	cleanup := func() {
		database.DB = nil
		os.Remove(dbPath)
		os.Remove(dbPath + "-shm")
		os.Remove(dbPath + "-wal")
	}

	return svc, user.ID, cleanup
}

func TestNewEncryptionService(t *testing.T) {
	svc := NewEncryptionService()
	assert.NotNil(t, svc)
}

func TestEncryptionService_SetupEncryption_Success(t *testing.T) {
	svc, userID, cleanup := setupEncryptionServiceTest(t)
	defer cleanup()

	req := &SetupEncryptionRequest{
		PasswordHint: "my favorite color",
	}

	resp, err := svc.SetupEncryption(userID, req)

	assert.NoError(t, err)
	assert.NotNil(t, resp)
	assert.True(t, resp.EncryptionEnabled)
	assert.NotEmpty(t, resp.Salt)

	// Verify salt is valid base64
	saltBytes, err := base64.StdEncoding.DecodeString(resp.Salt)
	assert.NoError(t, err)
	assert.Len(t, saltBytes, 32) // 32-byte salt

	// Verify settings were saved to database
	var settings model.EncryptionSettings
	err = database.DB.Where("user_id = ?", userID).First(&settings).Error
	assert.NoError(t, err)
	assert.True(t, settings.Enabled)
	assert.Equal(t, "my favorite color", settings.PasswordHint)
	assert.Len(t, settings.Salt, 32)
}

func TestEncryptionService_SetupEncryption_AlreadyEnabled(t *testing.T) {
	svc, userID, cleanup := setupEncryptionServiceTest(t)
	defer cleanup()

	// First setup
	req := &SetupEncryptionRequest{
		PasswordHint: "first hint",
	}
	_, err := svc.SetupEncryption(userID, req)
	require.NoError(t, err)

	// Second setup should fail
	req2 := &SetupEncryptionRequest{
		PasswordHint: "second hint",
	}
	resp, err := svc.SetupEncryption(userID, req2)

	assert.Error(t, err)
	assert.Equal(t, ErrEncryptionAlreadyEnabled, err)
	assert.Nil(t, resp)
}

func TestEncryptionService_SetupEncryption_ReenableAfterDisable(t *testing.T) {
	svc, userID, cleanup := setupEncryptionServiceTest(t)
	defer cleanup()

	// First setup
	req := &SetupEncryptionRequest{
		PasswordHint: "first hint",
	}
	resp, err := svc.SetupEncryption(userID, req)
	require.NoError(t, err)
	assert.True(t, resp.EncryptionEnabled)

	// Disable encryption
	database.DB.Model(&model.EncryptionSettings{}).
		Where("user_id = ?", userID).
		Update("enabled", false)

	// Re-enable should work
	req2 := &SetupEncryptionRequest{
		PasswordHint: "new hint",
	}
	resp, err = svc.SetupEncryption(userID, req2)

	assert.NoError(t, err)
	assert.NotNil(t, resp)
	assert.True(t, resp.EncryptionEnabled)

	// Verify new salt was generated
	var settings model.EncryptionSettings
	database.DB.Where("user_id = ?", userID).First(&settings)
	assert.Equal(t, "new hint", settings.PasswordHint)
}

func TestEncryptionService_GetEncryptionSalt_Success(t *testing.T) {
	svc, userID, cleanup := setupEncryptionServiceTest(t)
	defer cleanup()

	// Setup encryption first
	setupReq := &SetupEncryptionRequest{
		PasswordHint: "my hint",
	}
	setupResp, err := svc.SetupEncryption(userID, setupReq)
	require.NoError(t, err)

	// Get salt
	resp, err := svc.GetEncryptionSalt(userID)

	assert.NoError(t, err)
	assert.NotNil(t, resp)
	assert.True(t, resp.EncryptionEnabled)
	assert.Equal(t, setupResp.Salt, resp.Salt)
	assert.Equal(t, "my hint", resp.PasswordHint)
}

func TestEncryptionService_GetEncryptionSalt_NotSetup(t *testing.T) {
	svc, userID, cleanup := setupEncryptionServiceTest(t)
	defer cleanup()

	resp, err := svc.GetEncryptionSalt(userID)

	assert.Error(t, err)
	assert.Equal(t, ErrEncryptionNotSetup, err)
	assert.Nil(t, resp)
}

func TestEncryptionService_GetEncryptionSalt_Disabled(t *testing.T) {
	svc, userID, cleanup := setupEncryptionServiceTest(t)
	defer cleanup()

	// Setup encryption first
	setupReq := &SetupEncryptionRequest{
		PasswordHint: "hint",
	}
	_, err := svc.SetupEncryption(userID, setupReq)
	require.NoError(t, err)

	// Disable encryption
	database.DB.Model(&model.EncryptionSettings{}).
		Where("user_id = ?", userID).
		Update("enabled", false)

	// Get salt should fail
	resp, err := svc.GetEncryptionSalt(userID)

	assert.Error(t, err)
	assert.Equal(t, ErrEncryptionNotSetup, err)
	assert.Nil(t, resp)
}

func TestEncryptionService_MultipleUsers(t *testing.T) {
	svc, userID1, cleanup := setupEncryptionServiceTest(t)
	defer cleanup()

	// Create second user
	user2 := model.User{
		Username:     "testuser2",
		Email:        "test2@example.com",
		PasswordHash: "hashedpassword",
	}
	require.NoError(t, database.DB.Create(&user2).Error)

	// Setup encryption for both users
	req1 := &SetupEncryptionRequest{PasswordHint: "user1 hint"}
	req2 := &SetupEncryptionRequest{PasswordHint: "user2 hint"}

	resp1, err := svc.SetupEncryption(userID1, req1)
	require.NoError(t, err)

	resp2, err := svc.SetupEncryption(user2.ID, req2)
	require.NoError(t, err)

	// Each user should have different salts
	assert.NotEqual(t, resp1.Salt, resp2.Salt)

	// Verify each user can get their own salt
	salt1, err := svc.GetEncryptionSalt(userID1)
	require.NoError(t, err)
	assert.Equal(t, resp1.Salt, salt1.Salt)

	salt2, err := svc.GetEncryptionSalt(user2.ID)
	require.NoError(t, err)
	assert.Equal(t, resp2.Salt, salt2.Salt)
}

func TestEncryptionService_ErrorConstants(t *testing.T) {
	assert.Equal(t, "加密已启用，无需重复设置", ErrEncryptionAlreadyEnabled.Error())
	assert.Equal(t, "请先设置端到端加密密码", ErrEncryptionNotSetup.Error())
}

func TestEncryptionService_SaltUniqueness(t *testing.T) {
	svc, _, cleanup := setupEncryptionServiceTest(t)
	defer cleanup()

	// Create multiple users and verify salts are unique
	salts := make([]string, 10)
	for i := 0; i < 10; i++ {
		user := model.User{
			Username:     "user" + string(rune('0'+i)),
			Email:        "user" + string(rune('0'+i)) + "@example.com",
			PasswordHash: "hash",
		}
		require.NoError(t, database.DB.Create(&user).Error)

		resp, err := svc.SetupEncryption(user.ID, &SetupEncryptionRequest{PasswordHint: "hint"})
		require.NoError(t, err)
		salts[i] = resp.Salt
	}

	// Verify all salts are unique (with high probability)
	uniqueSalts := make(map[string]bool)
	for _, salt := range salts {
		uniqueSalts[salt] = true
	}
	assert.Len(t, uniqueSalts, 10, "All salts should be unique")
}

func TestEncryptionService_SaltLength(t *testing.T) {
	svc, userID, cleanup := setupEncryptionServiceTest(t)
	defer cleanup()

	resp, err := svc.SetupEncryption(userID, &SetupEncryptionRequest{PasswordHint: "hint"})
	require.NoError(t, err)

	// Decode salt and verify length
	saltBytes, err := base64.StdEncoding.DecodeString(resp.Salt)
	require.NoError(t, err)
	assert.Len(t, saltBytes, 32, "Salt should be 32 bytes")
}
