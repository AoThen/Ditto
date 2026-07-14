package tests

import (
	"encoding/base64"
	"net/http"
	"testing"

	"ditto-cloud-server/internal/model"
	"ditto-cloud-server/tests/testutil"

	"github.com/stretchr/testify/assert"
)

func TestEncryptionSetupAndRetrieve(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	defer server.Close()

	user := testutil.CreateTestUser(t, server)

	crypto := makeTestCrypto()
	crypto["password_hint"] = "my hint"
	statusCode, body := testutil.AuthPost(t, server, "/api/v1/encryption/setup", user.Token, crypto)
	assert.Equal(t, http.StatusOK, statusCode, "setup should succeed")

	code, _, data := testutil.ParseResponse(t, body)
	assert.Equal(t, 0, code, "response code should be 0")
	assert.True(t, data["encryption_enabled"].(bool), "encryption should be enabled")

	salt, _ := data["salt"].(string)
	assert.NotEmpty(t, salt, "salt should not be empty")

	_, err := base64.StdEncoding.DecodeString(salt)
	assert.NoError(t, err, "salt should be valid base64")

	statusCode2, body2 := testutil.AuthGet(t, server, "/api/v1/encryption/salt", user.Token)
	assert.Equal(t, http.StatusOK, statusCode2, "get salt should succeed")

	_, _, data2 := testutil.ParseResponse(t, body2)
	assert.Equal(t, salt, data2["salt"], "retrieved salt should match setup salt")
	assert.Equal(t, "my hint", data2["password_hint"], "password hint should match")
}

func TestEncryptionSetupDuplicate(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	defer server.Close()

	user := testutil.CreateTestUser(t, server)

	crypto := makeTestCrypto()
	statusCode1, _ := testutil.AuthPost(t, server, "/api/v1/encryption/setup", user.Token, crypto)
	assert.Equal(t, http.StatusOK, statusCode1, "first setup should succeed")

	crypto2 := makeTestCrypto()
	statusCode2, body2 := testutil.AuthPost(t, server, "/api/v1/encryption/setup", user.Token, crypto2)
	assert.Equal(t, http.StatusConflict, statusCode2, "duplicate setup should return conflict")

	code2, _, _ := testutil.ParseResponse(t, body2)
	assert.Equal(t, 40901, code2, "error code should be 40901")
}

func TestEndToEndEncryptionFlow(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	defer server.Close()

	user1 := testutil.CreateTestUser(t, server)

	crypto := makeTestCrypto()
	statusCode, body := testutil.AuthPost(t, server, "/api/v1/encryption/setup", user1.Token, crypto)
	assert.Equal(t, http.StatusOK, statusCode)

	_, _, data := testutil.ParseResponse(t, body)
	saltStr, _ := data["salt"].(string)

	saltBytes, err := base64.StdEncoding.DecodeString(saltStr)
	assert.NoError(t, err, "salt should be decodable")
	assert.Equal(t, 32, len(saltBytes), "salt should be 32 bytes")

	salt2 := make([]byte, 32)
	for i := range salt2 {
		salt2[i] = byte(i)
	}
	assert.NotEqual(t, saltBytes, salt2, "server salt should not be predictable")

	user2 := testutil.CreateTestUser(t, server)
	crypto2 := makeTestCrypto()
	statusCode2, body2 := testutil.AuthPost(t, server, "/api/v1/encryption/setup", user2.Token, crypto2)
	assert.Equal(t, http.StatusOK, statusCode2)

	_, _, data2 := testutil.ParseResponse(t, body2)
	saltStr2, _ := data2["salt"].(string)

	assert.NotEqual(t, saltStr, saltStr2, "different users should have different salts")
}

func TestEncryptionSettingsPersistence(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	defer server.Close()

	user := testutil.CreateTestUser(t, server)

	crypto := makeTestCrypto()
	_, _ = testutil.AuthPost(t, server, "/api/v1/encryption/setup", user.Token, crypto)

	var settings model.EncryptionSettings
	err := testutil.TestDB.Where("user_id = ?", getUserIDFromToken(t, user.Token)).First(&settings).Error
	assert.NoError(t, err, "encryption settings should exist in DB")
	assert.True(t, settings.Enabled, "encryption should be enabled")
	assert.Equal(t, 32, len(settings.Salt), "salt should be 32 bytes")
	assert.Equal(t, 32, len(settings.WrappedDEK), "wrapped_dek should be present")
}

func getUserIDFromToken(t *testing.T, token string) uint {
	t.Helper()
	var user model.User
	if err := testutil.TestDB.First(&user).Error; err != nil {
		t.Fatalf("failed to get user from DB: %v", err)
	}
	return user.ID
}
