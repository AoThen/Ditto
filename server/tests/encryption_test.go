package tests

import (
	"encoding/base64"
	"net/http"
	"testing"

	"ditto-cloud-server/internal/model"
	"ditto-cloud-server/tests/testutil"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

func makeTestCrypto() map[string]string {
	dek := make([]byte, 32)
	vhash := make([]byte, 32)
	for i := range dek {
		dek[i] = byte(i)
		vhash[i] = byte(0xFF - i)
	}
	return map[string]string{
		"wrapped_dek":       base64.StdEncoding.EncodeToString(dek),
		"verification_hash": base64.StdEncoding.EncodeToString(vhash),
	}
}

func TestEncryption_Setup(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	user := testutil.CreateTestUser(t, server)

	crypto := makeTestCrypto()
	crypto["password_hint"] = "my favorite color"

	statusCode, respBody := testutil.AuthPost(t, server, "/api/v1/encryption/setup", user.Token, crypto)
	assert.Equal(t, http.StatusOK, statusCode)
	code, message, data := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)
	assert.Equal(t, "加密已启用", message)
	assert.NotNil(t, data)

	salt, ok := data["salt"].(string)
	require.True(t, ok, "response should contain salt")
	assert.NotEmpty(t, salt, "salt should not be empty")

	enabled, ok := data["encryption_enabled"].(bool)
	require.True(t, ok, "response should contain encryption_enabled")
	assert.True(t, enabled, "encryption should be enabled")
}

func TestEncryption_GetSalt(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	user := testutil.CreateTestUser(t, server)

	crypto := makeTestCrypto()
	crypto["password_hint"] = "my hint"
	statusCode, respBody := testutil.AuthPost(t, server, "/api/v1/encryption/setup", user.Token, crypto)
	require.Equal(t, http.StatusOK, statusCode)
	code, _, setupData := testutil.ParseResponse(t, respBody)
	require.Equal(t, 0, code)

	expectedSalt := setupData["salt"].(string)

	statusCode, respBody = testutil.AuthGet(t, server, "/api/v1/encryption/salt", user.Token)
	assert.Equal(t, http.StatusOK, statusCode)
	code, _, getData := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)

	actualSalt := getData["salt"].(string)
	assert.Equal(t, expectedSalt, actualSalt, "salt should be the same after setup")

	enabled, ok := getData["encryption_enabled"].(bool)
	require.True(t, ok)
	assert.True(t, enabled)

	hint, ok := getData["password_hint"].(string)
	require.True(t, ok)
	assert.Equal(t, "my hint", hint)
}

func TestEncryption_DuplicateSetup(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	user := testutil.CreateTestUser(t, server)

	crypto := makeTestCrypto()
	crypto["password_hint"] = "first hint"
	statusCode, respBody := testutil.AuthPost(t, server, "/api/v1/encryption/setup", user.Token, crypto)
	require.Equal(t, http.StatusOK, statusCode)
	code, _, setupData := testutil.ParseResponse(t, respBody)
	require.Equal(t, 0, code)
	firstSalt := setupData["salt"].(string)

	crypto2 := makeTestCrypto()
	statusCode, respBody = testutil.AuthPost(t, server, "/api/v1/encryption/setup", user.Token, crypto2)
	assert.Equal(t, http.StatusConflict, statusCode)
	code, message, _ := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 40901, code)
	assert.Contains(t, message, "加密已启用")

	statusCode, respBody = testutil.AuthGet(t, server, "/api/v1/encryption/salt", user.Token)
	require.Equal(t, http.StatusOK, statusCode)
	_, _, getData := testutil.ParseResponse(t, respBody)
	assert.Equal(t, firstSalt, getData["salt"].(string))
}

func TestEncryption_GetSaltBeforeSetup(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	user := testutil.CreateTestUser(t, server)

	statusCode, respBody := testutil.AuthGet(t, server, "/api/v1/encryption/salt", user.Token)
	assert.Equal(t, http.StatusOK, statusCode)
	code, _, data := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)
	assert.NotEmpty(t, data["salt"].(string))
	assert.False(t, data["encryption_enabled"].(bool))
}

func TestEncryption_KeyMaterial_Success(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	user := testutil.CreateTestUser(t, server)

	crypto := makeTestCrypto()
	_, _ = testutil.AuthPost(t, server, "/api/v1/encryption/setup", user.Token, crypto)

	statusCode, respBody := testutil.AuthGet(t, server, "/api/v1/encryption/key-material", user.Token)
	assert.Equal(t, http.StatusOK, statusCode)
	code, _, data := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)
	assert.Equal(t, crypto["wrapped_dek"], data["wrapped_dek"].(string))
	assert.NotEmpty(t, data["salt"].(string))
}

func TestEncryption_KeyMaterial_NotSetup(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	user := testutil.CreateTestUser(t, server)

	statusCode, _ := testutil.AuthGet(t, server, "/api/v1/encryption/key-material", user.Token)
	assert.Equal(t, http.StatusNotFound, statusCode)
}

func TestEncryption_ChangePassword_Success(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	user := testutil.CreateTestUser(t, server)

	crypto := makeTestCrypto()
	_, _ = testutil.AuthPost(t, server, "/api/v1/encryption/setup", user.Token, crypto)

	// Verify old hash works
	newDEK := make([]byte, 32)
	newVHash := make([]byte, 32)
	for i := range newDEK {
		newDEK[i] = byte(i + 10)
		newVHash[i] = byte(0xEE - i)
	}
	newSalt := make([]byte, 32)
	for i := range newSalt {
		newSalt[i] = byte(i)
	}
	changeReq := map[string]string{
		"old_verification_hash": crypto["verification_hash"],
		"new_salt":              base64.StdEncoding.EncodeToString(newSalt),
		"new_wrapped_dek":       base64.StdEncoding.EncodeToString(newDEK),
		"new_verification_hash": base64.StdEncoding.EncodeToString(newVHash),
		"new_password_hint":     "new hint",
	}
	statusCode, respBody := testutil.AuthPost(t, server, "/api/v1/encryption/change-password", user.Token, changeReq)
	assert.Equal(t, http.StatusOK, statusCode)
	code, _, data := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)
	assert.NotEmpty(t, data["salt"].(string))
}

func TestEncryption_ChangePassword_WrongOldHash(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	user := testutil.CreateTestUser(t, server)

	crypto := makeTestCrypto()
	_, _ = testutil.AuthPost(t, server, "/api/v1/encryption/setup", user.Token, crypto)

	wrongHash := base64.StdEncoding.EncodeToString([]byte("this-is-a-wrong-hash-value-12345"))
	changeReq := map[string]string{
		"old_verification_hash": wrongHash,
		"new_salt":              base64.StdEncoding.EncodeToString(make([]byte, 32)),
		"new_wrapped_dek":       base64.StdEncoding.EncodeToString(make([]byte, 32)),
		"new_verification_hash": base64.StdEncoding.EncodeToString(make([]byte, 32)),
	}
	statusCode, _ := testutil.AuthPost(t, server, "/api/v1/encryption/change-password", user.Token, changeReq)
	assert.Equal(t, http.StatusForbidden, statusCode)
}

func TestEncryption_Disable(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	user := testutil.CreateTestUser(t, server)

	crypto := makeTestCrypto()
	_, _ = testutil.AuthPost(t, server, "/api/v1/encryption/setup", user.Token, crypto)

	statusCode, _ := testutil.AuthPost(t, server, "/api/v1/encryption/disable", user.Token, map[string]string{
		"verification_hash": crypto["verification_hash"],
	})
	assert.Equal(t, http.StatusOK, statusCode)
}

func TestEncryption_Persistence(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	user := testutil.CreateTestUser(t, server)

	crypto := makeTestCrypto()
	_, _ = testutil.AuthPost(t, server, "/api/v1/encryption/setup", user.Token, crypto)

	var settings model.EncryptionSettings
	err := testutil.TestDB.Where("user_id = ?", getUserIDFromToken(t, user)).First(&settings).Error
	assert.NoError(t, err, "encryption settings should exist in DB")
	assert.True(t, settings.Enabled, "encryption should be enabled")
	assert.Equal(t, 32, len(settings.Salt), "salt should be 32 bytes")
	assert.Equal(t, 32, len(settings.WrappedDEK), "wrapped_dek should match input size (32 raw)")
	assert.Equal(t, 32, len(settings.VerificationHash), "verification_hash should be 32 bytes")
	assert.Equal(t, 2, settings.Version, "version should be 2")
}
