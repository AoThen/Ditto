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

func setupEncryptionServiceTest(t *testing.T) (*EncryptionService, uint, func()) {
	t.Helper()

	tmpFile, err := os.CreateTemp("", "encryption_service_test_*.db")
	require.NoError(t, err)
	dbPath := tmpFile.Name()
	tmpFile.Close()

	err = database.Init(dbPath)
	require.NoError(t, err)

	user := model.User{
		Username:     "testuser",
		Email:        "test@example.com",
		PasswordHash: "hashedpassword",
	}
	require.NoError(t, database.DB.Create(&user).Error)

	svc := NewEncryptionService()

	cleanup := func() {
		database.DB = nil
		os.Remove(dbPath)
		os.Remove(dbPath + "-shm")
		os.Remove(dbPath + "-wal")
	}

	return svc, user.ID, cleanup
}

func makeTestDEK() (string, string) {
	dek := make([]byte, 32)
	for i := range dek {
		dek[i] = byte(i)
	}
	vhash := make([]byte, 32)
	for i := range vhash {
		vhash[i] = byte(0xFF - i)
	}
	return base64.StdEncoding.EncodeToString(dek), base64.StdEncoding.EncodeToString(vhash)
}

func TestNewEncryptionService(t *testing.T) {
	svc := NewEncryptionService()
	assert.NotNil(t, svc)
}

func TestEncryptionService_SetupEncryption_Success(t *testing.T) {
	svc, userID, cleanup := setupEncryptionServiceTest(t)
	defer cleanup()

	wrappedDEK, vhash := makeTestDEK()
	req := &SetupEncryptionRequest{
		WrappedDEK:       wrappedDEK,
		VerificationHash: vhash,
		PasswordHint:     "my favorite color",
	}

	resp, err := svc.SetupEncryption(userID, req)

	assert.NoError(t, err)
	assert.NotNil(t, resp)
	assert.True(t, resp.EncryptionEnabled)
	assert.NotEmpty(t, resp.Salt)

	saltBytes, err := base64.StdEncoding.DecodeString(resp.Salt)
	assert.NoError(t, err)
	assert.Len(t, saltBytes, 32)

	var settings model.EncryptionSettings
	err = database.DB.Where("user_id = ?", userID).First(&settings).Error
	assert.NoError(t, err)
	assert.True(t, settings.Enabled)
	assert.Equal(t, "my favorite color", settings.PasswordHint)
	assert.Len(t, settings.Salt, 32)
	assert.Len(t, settings.WrappedDEK, 32)
	assert.Len(t, settings.VerificationHash, 32)
	assert.Equal(t, 2, settings.Version)
}

func TestEncryptionService_SetupEncryption_AlreadyEnabled(t *testing.T) {
	svc, userID, cleanup := setupEncryptionServiceTest(t)
	defer cleanup()

	wrappedDEK, vhash := makeTestDEK()
	req := &SetupEncryptionRequest{WrappedDEK: wrappedDEK, VerificationHash: vhash, PasswordHint: "first hint"}
	_, err := svc.SetupEncryption(userID, req)
	require.NoError(t, err)

	req2 := &SetupEncryptionRequest{WrappedDEK: wrappedDEK, VerificationHash: vhash, PasswordHint: "second hint"}
	resp, err := svc.SetupEncryption(userID, req2)

	assert.Error(t, err)
	assert.Equal(t, ErrEncryptionAlreadyEnabled, err)
	assert.Nil(t, resp)
}

func TestEncryptionService_SetupEncryption_ReenableAfterDisable(t *testing.T) {
	svc, userID, cleanup := setupEncryptionServiceTest(t)
	defer cleanup()

	wrappedDEK, vhash := makeTestDEK()
	req := &SetupEncryptionRequest{WrappedDEK: wrappedDEK, VerificationHash: vhash, PasswordHint: "first hint"}
	resp, err := svc.SetupEncryption(userID, req)
	require.NoError(t, err)
	assert.True(t, resp.EncryptionEnabled)

	database.DB.Model(&model.EncryptionSettings{}).
		Where("user_id = ?", userID).
		Update("enabled", false)

	wrappedDEK2, vhash2 := makeTestDEK()
	req2 := &SetupEncryptionRequest{WrappedDEK: wrappedDEK2, VerificationHash: vhash2, PasswordHint: "new hint"}
	resp, err = svc.SetupEncryption(userID, req2)

	assert.NoError(t, err)
	assert.NotNil(t, resp)
	assert.True(t, resp.EncryptionEnabled)

	var settings model.EncryptionSettings
	database.DB.Where("user_id = ?", userID).First(&settings)
	assert.Equal(t, "new hint", settings.PasswordHint)
}

func TestEncryptionService_GetEncryptionSalt_Success(t *testing.T) {
	svc, userID, cleanup := setupEncryptionServiceTest(t)
	defer cleanup()

	wrappedDEK, vhash := makeTestDEK()
	setupReq := &SetupEncryptionRequest{WrappedDEK: wrappedDEK, VerificationHash: vhash, PasswordHint: "my hint"}
	setupResp, err := svc.SetupEncryption(userID, setupReq)
	require.NoError(t, err)

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

	assert.NoError(t, err)
	assert.NotNil(t, resp)
	assert.False(t, resp.EncryptionEnabled)
	assert.NotEmpty(t, resp.Salt)
}

func TestEncryptionService_GetKeyMaterial_Success(t *testing.T) {
	svc, userID, cleanup := setupEncryptionServiceTest(t)
	defer cleanup()

	wrappedDEK, vhash := makeTestDEK()
	_, err := svc.SetupEncryption(userID, &SetupEncryptionRequest{WrappedDEK: wrappedDEK, VerificationHash: vhash})
	require.NoError(t, err)

	resp, err := svc.GetKeyMaterial(userID)

	assert.NoError(t, err)
	assert.NotNil(t, resp)
	assert.Equal(t, wrappedDEK, resp.WrappedDEK)
	assert.NotEmpty(t, resp.Salt)
}

func TestEncryptionService_GetKeyMaterial_NotSetup(t *testing.T) {
	svc, userID, cleanup := setupEncryptionServiceTest(t)
	defer cleanup()

	resp, err := svc.GetKeyMaterial(userID)

	assert.Error(t, err)
	assert.Equal(t, ErrEncryptionNotSetup, err)
	assert.Nil(t, resp)
}

func TestEncryptionService_ChangeEncryptionPassword_Success(t *testing.T) {
	svc, userID, cleanup := setupEncryptionServiceTest(t)
	defer cleanup()

	wrappedDEK, vhash := makeTestDEK()
	_, err := svc.SetupEncryption(userID, &SetupEncryptionRequest{WrappedDEK: wrappedDEK, VerificationHash: vhash, PasswordHint: "old hint"})
	require.NoError(t, err)

	newWrappedDEK, newVhash := makeTestDEK()
	newSaltB64 := base64.StdEncoding.EncodeToString([]byte("01234567890123456789012345678901"))
	req := &ChangePasswordRequest{
		OldVerificationHash: vhash,
		NewSalt:             newSaltB64,
		NewWrappedDEK:       newWrappedDEK,
		NewVerificationHash: newVhash,
		NewPasswordHint:     "new hint",
	}

	resp, err := svc.ChangeEncryptionPassword(userID, req)

	assert.NoError(t, err)
	assert.NotNil(t, resp)
	assert.True(t, resp.EncryptionEnabled)

	var settings model.EncryptionSettings
	database.DB.Where("user_id = ?", userID).First(&settings)
	assert.Equal(t, "new hint", settings.PasswordHint)
	assert.Equal(t, newWrappedDEK, base64.StdEncoding.EncodeToString(settings.WrappedDEK))
}

func TestEncryptionService_ChangeEncryptionPassword_WrongHash(t *testing.T) {
	svc, userID, cleanup := setupEncryptionServiceTest(t)
	defer cleanup()

	wrappedDEK, vhash := makeTestDEK()
	_, err := svc.SetupEncryption(userID, &SetupEncryptionRequest{WrappedDEK: wrappedDEK, VerificationHash: vhash})
	require.NoError(t, err)

	wrongHash := base64.StdEncoding.EncodeToString([]byte("wrong-hash-value-32-bytes-long!"))
	newWrappedDEK, newVhash := makeTestDEK()
	req := &ChangePasswordRequest{
		OldVerificationHash: wrongHash,
		NewSalt:             base64.StdEncoding.EncodeToString(make([]byte, 32)),
		NewWrappedDEK:       newWrappedDEK,
		NewVerificationHash: newVhash,
	}

	resp, err := svc.ChangeEncryptionPassword(userID, req)

	assert.Error(t, err)
	assert.Equal(t, ErrInvalidVerificationHash, err)
	assert.Nil(t, resp)
}

func TestEncryptionService_DisableEncryption(t *testing.T) {
	svc, userID, cleanup := setupEncryptionServiceTest(t)
	defer cleanup()

	wrappedDEK, vhash := makeTestDEK()
	_, err := svc.SetupEncryption(userID, &SetupEncryptionRequest{WrappedDEK: wrappedDEK, VerificationHash: vhash})
	require.NoError(t, err)

	err = svc.DisableEncryption(userID)
	assert.NoError(t, err)

	var settings model.EncryptionSettings
	database.DB.Where("user_id = ?", userID).First(&settings)
	assert.False(t, settings.Enabled)
}

func TestEncryptionService_MultipleUsers(t *testing.T) {
	svc, userID1, cleanup := setupEncryptionServiceTest(t)
	defer cleanup()

	user2 := model.User{
		Username:     "testuser2",
		Email:        "test2@example.com",
		PasswordHash: "hashedpassword",
	}
	require.NoError(t, database.DB.Create(&user2).Error)

	wrappedDEK1, vhash1 := makeTestDEK()
	wrappedDEK2, vhash2 := makeTestDEK()
	req1 := &SetupEncryptionRequest{WrappedDEK: wrappedDEK1, VerificationHash: vhash1, PasswordHint: "user1 hint"}
	req2 := &SetupEncryptionRequest{WrappedDEK: wrappedDEK2, VerificationHash: vhash2, PasswordHint: "user2 hint"}

	resp1, err := svc.SetupEncryption(userID1, req1)
	require.NoError(t, err)

	resp2, err := svc.SetupEncryption(user2.ID, req2)
	require.NoError(t, err)

	assert.NotEqual(t, resp1.Salt, resp2.Salt)

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
	assert.Equal(t, "旧密码验证失败", ErrInvalidVerificationHash.Error())
}

func TestEncryptionService_SaltUniqueness(t *testing.T) {
	svc, _, cleanup := setupEncryptionServiceTest(t)
	defer cleanup()

	salts := make([]string, 10)
	for i := 0; i < 10; i++ {
		user := model.User{
			Username:     "user" + string(rune('0'+i)),
			Email:        "user" + string(rune('0'+i)) + "@example.com",
			PasswordHash: "hash",
		}
		require.NoError(t, database.DB.Create(&user).Error)

		wrappedDEK, vhash := makeTestDEK()
		resp, err := svc.SetupEncryption(user.ID, &SetupEncryptionRequest{WrappedDEK: wrappedDEK, VerificationHash: vhash, PasswordHint: "hint"})
		require.NoError(t, err)
		salts[i] = resp.Salt
	}

	uniqueSalts := make(map[string]bool)
	for _, salt := range salts {
		uniqueSalts[salt] = true
	}
	assert.Len(t, uniqueSalts, 10, "All salts should be unique")
}

func TestEncryptionService_SaltLength(t *testing.T) {
	svc, userID, cleanup := setupEncryptionServiceTest(t)
	defer cleanup()

	wrappedDEK, vhash := makeTestDEK()
	resp, err := svc.SetupEncryption(userID, &SetupEncryptionRequest{WrappedDEK: wrappedDEK, VerificationHash: vhash, PasswordHint: "hint"})
	require.NoError(t, err)

	saltBytes, err := base64.StdEncoding.DecodeString(resp.Salt)
	require.NoError(t, err)
	assert.Len(t, saltBytes, 32, "Salt should be 32 bytes")
}

func TestEncryptionService_GetEncryptionSalt_GeneratesSaltForNewUser(t *testing.T) {
	svc, _, cleanup := setupEncryptionServiceTest(t)
	defer cleanup()

	user := model.User{
		Username:     "newuser",
		Email:        "new@example.com",
		PasswordHash: "hash",
	}
	require.NoError(t, database.DB.Create(&user).Error)

	resp, err := svc.GetEncryptionSalt(user.ID)

	assert.NoError(t, err)
	assert.NotNil(t, resp)
	assert.NotEmpty(t, resp.Salt)
	assert.False(t, resp.EncryptionEnabled)
}