package service

import (
	"encoding/base64"
	"os"
	"testing"
	"time"

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

	err = database.Init(dbPath, 500*time.Millisecond)
	require.NoError(t, err)

	user := model.User{
		Username:     "encuser",
		Email:        "enc@example.com",
		PasswordHash: "hash",
	}
	err = database.DB.Create(&user).Error
	require.NoError(t, err)

	svc := NewEncryptionService()

	cleanup := func() {
		database.DB = nil
		os.Remove(dbPath)
		os.Remove(dbPath + "-shm")
		os.Remove(dbPath + "-wal")
	}

	return svc, user.ID, cleanup
}

func makeTestDEK() (wrappedDEK, verificationHash string) {
	dek := make([]byte, 32)
	vhash := make([]byte, 32)
	for i := range dek {
		dek[i] = byte(i)
		vhash[i] = byte(0xFF - i)
	}
	return base64.StdEncoding.EncodeToString(dek),
		base64.StdEncoding.EncodeToString(vhash)
}

func TestEncryptionService_Setup_Success(t *testing.T) {
	svc, userID, cleanup := setupEncryptionServiceTest(t)
	defer cleanup()

	wrappedDEK, vhash := makeTestDEK()
	req := &SetupEncryptionRequest{
		WrappedDEK:       wrappedDEK,
		VerificationHash: vhash,
		PasswordHint:     "my hint",
	}

	resp, err := svc.SetupEncryption(userID, req)

	assert.NoError(t, err)
	assert.NotNil(t, resp)
	assert.NotEmpty(t, resp.Salt)
	assert.True(t, resp.EncryptionEnabled)
}

func TestEncryptionService_Setup_Duplicate(t *testing.T) {
	svc, userID, cleanup := setupEncryptionServiceTest(t)
	defer cleanup()

	wrappedDEK, vhash := makeTestDEK()
	req := &SetupEncryptionRequest{
		WrappedDEK:       wrappedDEK,
		VerificationHash: vhash,
	}

	_, err := svc.SetupEncryption(userID, req)
	require.NoError(t, err)

	_, err = svc.SetupEncryption(userID, req)
	assert.Error(t, err)
	assert.Equal(t, ErrEncryptionAlreadyEnabled, err)
}

func TestEncryptionService_Setup_InvalidWrappedDEK(t *testing.T) {
	svc, userID, cleanup := setupEncryptionServiceTest(t)
	defer cleanup()

	req := &SetupEncryptionRequest{
		WrappedDEK:       "not-valid-base64!!!",
		VerificationHash: base64.StdEncoding.EncodeToString(make([]byte, 32)),
	}

	_, err := svc.SetupEncryption(userID, req)
	assert.Error(t, err)
}

func TestEncryptionService_GetSalt_BeforeSetup(t *testing.T) {
	svc, userID, cleanup := setupEncryptionServiceTest(t)
	defer cleanup()

	resp, err := svc.GetEncryptionSalt(userID)

	assert.NoError(t, err)
	assert.NotEmpty(t, resp.Salt)
	assert.False(t, resp.EncryptionEnabled)
	assert.Empty(t, resp.PasswordHint)
}

func TestEncryptionService_GetSalt_AfterSetup(t *testing.T) {
	svc, userID, cleanup := setupEncryptionServiceTest(t)
	defer cleanup()

	wrappedDEK, vhash := makeTestDEK()
	_, err := svc.SetupEncryption(userID, &SetupEncryptionRequest{
		WrappedDEK:       wrappedDEK,
		VerificationHash: vhash,
		PasswordHint:     "my hint",
	})
	require.NoError(t, err)

	resp, err := svc.GetEncryptionSalt(userID)

	assert.NoError(t, err)
	assert.NotEmpty(t, resp.Salt)
	assert.True(t, resp.EncryptionEnabled)
	assert.Equal(t, "my hint", resp.PasswordHint)
}

func TestEncryptionService_GetKeyMaterial_NotSetup(t *testing.T) {
	svc, userID, cleanup := setupEncryptionServiceTest(t)
	defer cleanup()

	_, err := svc.GetKeyMaterial(userID)

	assert.Error(t, err)
	assert.Equal(t, ErrEncryptionNotSetup, err)
}

func TestEncryptionService_GetKeyMaterial_AfterSetup(t *testing.T) {
	svc, userID, cleanup := setupEncryptionServiceTest(t)
	defer cleanup()

	wrappedDEK, vhash := makeTestDEK()
	_, err := svc.SetupEncryption(userID, &SetupEncryptionRequest{
		WrappedDEK:       wrappedDEK,
		VerificationHash: vhash,
	})
	require.NoError(t, err)

	resp, err := svc.GetKeyMaterial(userID)

	assert.NoError(t, err)
	assert.NotEmpty(t, resp.Salt)
	assert.Equal(t, wrappedDEK, resp.WrappedDEK)
}

func TestEncryptionService_ChangePassword_Success(t *testing.T) {
	svc, userID, cleanup := setupEncryptionServiceTest(t)
	defer cleanup()

	wrappedDEK, vhash := makeTestDEK()
	setupResp, err := svc.SetupEncryption(userID, &SetupEncryptionRequest{
		WrappedDEK:       wrappedDEK,
		VerificationHash: vhash,
	})
	require.NoError(t, err)

	newDEK := base64.StdEncoding.EncodeToString(make([]byte, 32))
	newVHash := base64.StdEncoding.EncodeToString(make([]byte, 32))
	newSalt := base64.StdEncoding.EncodeToString(make([]byte, 32))
	changeReq := &ChangePasswordRequest{
		OldVerificationHash: vhash,
		NewSalt:             newSalt,
		NewWrappedDEK:       newDEK,
		NewVerificationHash: newVHash,
		NewPasswordHint:     "new hint",
	}

	resp, err := svc.ChangeEncryptionPassword(userID, changeReq)

	assert.NoError(t, err)
	assert.NotEqual(t, setupResp.Salt, resp.Salt)
	assert.True(t, resp.EncryptionEnabled)
}

func TestEncryptionService_ChangePassword_WrongOldHash(t *testing.T) {
	svc, userID, cleanup := setupEncryptionServiceTest(t)
	defer cleanup()

	wrappedDEK, vhash := makeTestDEK()
	_, err := svc.SetupEncryption(userID, &SetupEncryptionRequest{
		WrappedDEK:       wrappedDEK,
		VerificationHash: vhash,
	})
	require.NoError(t, err)

	wrongHash := base64.StdEncoding.EncodeToString([]byte("this-is-wrong"))
	changeReq := &ChangePasswordRequest{
		OldVerificationHash: wrongHash,
		NewSalt:             base64.StdEncoding.EncodeToString(make([]byte, 32)),
		NewWrappedDEK:       base64.StdEncoding.EncodeToString(make([]byte, 32)),
		NewVerificationHash: base64.StdEncoding.EncodeToString(make([]byte, 32)),
	}

	_, err = svc.ChangeEncryptionPassword(userID, changeReq)
	assert.Equal(t, ErrInvalidVerificationHash, err)
}

func TestEncryptionService_Disable_Success(t *testing.T) {
	svc, userID, cleanup := setupEncryptionServiceTest(t)
	defer cleanup()

	wrappedDEK, vhash := makeTestDEK()
	_, err := svc.SetupEncryption(userID, &SetupEncryptionRequest{
		WrappedDEK:       wrappedDEK,
		VerificationHash: vhash,
	})
	require.NoError(t, err)

	err = svc.DisableEncryption(userID, &DisableEncryptionRequest{VerificationHash: vhash})
	assert.NoError(t, err)

	resp, err := svc.GetEncryptionSalt(userID)
	assert.NoError(t, err)
	assert.False(t, resp.EncryptionEnabled)
}

func TestEncryptionService_Disable_NotSetup(t *testing.T) {
	svc, userID, cleanup := setupEncryptionServiceTest(t)
	defer cleanup()

	err := svc.DisableEncryption(userID, &DisableEncryptionRequest{VerificationHash: "dGVzdA=="})
	assert.Equal(t, ErrEncryptionNotSetup, err)
}

func TestEncryptionService_Persistence(t *testing.T) {
	svc, userID, cleanup := setupEncryptionServiceTest(t)
	defer cleanup()

	wrappedDEK, vhash := makeTestDEK()
	_, err := svc.SetupEncryption(userID, &SetupEncryptionRequest{
		WrappedDEK:       wrappedDEK,
		VerificationHash: vhash,
	})
	require.NoError(t, err)

	var settings model.EncryptionSettings
	err = database.DB.Where("user_id = ?", userID).First(&settings).Error
	assert.NoError(t, err)
	assert.True(t, settings.Enabled)
	assert.Equal(t, 32, len(settings.Salt))
	assert.Equal(t, 2, settings.Version)
}

// TestEncryptionService_StoresBcryptWrappedHash verifies the password
// verification hash is wrapped at rest and still verifies through the wrapper.
func TestEncryptionService_StoresBcryptWrappedHash(t *testing.T) {
	svc, userID, cleanup := setupEncryptionServiceTest(t)
	defer cleanup()

	wrappedDEK, vhash := makeTestDEK()
	_, err := svc.SetupEncryption(userID, &SetupEncryptionRequest{
		WrappedDEK:       wrappedDEK,
		VerificationHash: vhash,
	})
	require.NoError(t, err)

	var settings model.EncryptionSettings
	require.NoError(t, database.DB.Where("user_id = ?", userID).First(&settings).Error)

	stored := string(settings.VerificationHash)
	assert.NotEqual(t, vhash, stored, "the raw hash must not be stored verbatim")
	assert.Equal(t, byte('$'), stored[0], "the stored value should be a bcrypt hash")

	// Verifying through the wrapper must still succeed.
	assert.NoError(t, svc.DisableEncryption(userID, &DisableEncryptionRequest{VerificationHash: vhash}))
}

// TestEncryptionService_Disable_UpgradesLegacyHash simulates a row written
// before the upgrade: an unwrapped hash must verify and be re-wrapped.
func TestEncryptionService_Disable_UpgradesLegacyHash(t *testing.T) {
	svc, userID, cleanup := setupEncryptionServiceTest(t)
	defer cleanup()

	wrappedDEK, vhash := makeTestDEK()
	rawHash, err := base64.StdEncoding.DecodeString(vhash)
	require.NoError(t, err)
	settings := model.EncryptionSettings{
		UserID:           userID,
		Version:          2,
		Salt:             []byte{1, 2, 3},
		WrappedDEK:       []byte(wrappedDEK),
		VerificationHash: rawHash,
		Enabled:          true,
	}
	require.NoError(t, database.DB.Create(&settings).Error)

	require.NoError(t, svc.DisableEncryption(userID, &DisableEncryptionRequest{VerificationHash: vhash}))

	var updated model.EncryptionSettings
	require.NoError(t, database.DB.Where("user_id = ?", userID).First(&updated).Error)
	assert.Equal(t, byte('$'), updated.VerificationHash[0], "the legacy hash must be re-wrapped")

	// Re-enable and verify again against the now-wrapped value.
	_, err = svc.SetupEncryption(userID, &SetupEncryptionRequest{
		WrappedDEK:       wrappedDEK,
		VerificationHash: vhash,
	})
	require.NoError(t, err)
	require.NoError(t, svc.DisableEncryption(userID, &DisableEncryptionRequest{VerificationHash: vhash}))
}

func TestEncryptionService_RejectsOversizedWrappedDEK(t *testing.T) {
	svc, userID, cleanup := setupEncryptionServiceTest(t)
	defer cleanup()

	_, vhash := makeTestDEK()
	over := make([]byte, maxEncWrappedDEKLen+1)
	for i := range over {
		over[i] = byte(i)
	}
	_, err := svc.SetupEncryption(userID, &SetupEncryptionRequest{
		WrappedDEK:       base64.StdEncoding.EncodeToString(over),
		VerificationHash: vhash,
	})
	assert.ErrorIs(t, err, ErrInvalidEncPayload)
}

func TestEncryptionService_RejectsOversizedSalt(t *testing.T) {
	svc, userID, cleanup := setupEncryptionServiceTest(t)
	defer cleanup()

	wrappedDEK, vhash := makeTestDEK()
	_, err := svc.SetupEncryption(userID, &SetupEncryptionRequest{
		WrappedDEK:       wrappedDEK,
		VerificationHash: vhash,
	})
	require.NoError(t, err)

	over := make([]byte, maxEncSaltLen+1)
	for i := range over {
		over[i] = byte(i)
	}
	newWrappedDEK, newVHash := makeTestDEK()
	_, err = svc.ChangeEncryptionPassword(userID, &ChangePasswordRequest{
		OldVerificationHash: vhash,
		NewWrappedDEK:       newWrappedDEK,
		NewSalt:             base64.StdEncoding.EncodeToString(over),
		NewVerificationHash: newVHash,
	})
	assert.ErrorIs(t, err, ErrInvalidEncPayload)
}

func TestEncryptionService_RejectsMalformedPasswordHint(t *testing.T) {
	svc, userID, cleanup := setupEncryptionServiceTest(t)
	defer cleanup()

	wrappedDEK, vhash := makeTestDEK()
	hint := string([]byte{0x00, 0x7F}) // NUL and DEL are control characters
	_, err := svc.SetupEncryption(userID, &SetupEncryptionRequest{
		WrappedDEK:       wrappedDEK,
		VerificationHash: vhash,
		PasswordHint:     hint,
	})
	assert.ErrorIs(t, err, ErrInvalidEncPayload)
}