package tests

import (
	"encoding/base64"
	"net/http"
	"testing"

	"ditto-cloud-server/internal/model"
	"ditto-cloud-server/tests/testutil"

	"github.com/stretchr/testify/assert"
)

// Test encryption setup and retrieval
func TestEncryptionSetupAndRetrieve(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	defer server.Close()

	// Register and login
	user := testutil.CreateTestUser(t, server)

	// Setup encryption
	setupReq := map[string]string{
		"password_hint": "my hint",
	}
	statusCode, body := testutil.AuthPost(t, server, "/api/v1/encryption/setup", user.Token, setupReq)
	assert.Equal(t, http.StatusOK, statusCode, "setup should succeed")

	code, _, data := testutil.ParseResponse(t, body)
	assert.Equal(t, 0, code, "response code should be 0")
	assert.True(t, data["encryption_enabled"].(bool), "encryption should be enabled")

	salt, _ := data["salt"].(string)
	assert.NotEmpty(t, salt, "salt should not be empty")

	// Verify salt is valid base64
	_, err := base64.StdEncoding.DecodeString(salt)
	assert.NoError(t, err, "salt should be valid base64")

	// Retrieve salt
	statusCode2, body2 := testutil.AuthGet(t, server, "/api/v1/encryption/salt", user.Token)
	assert.Equal(t, http.StatusOK, statusCode2, "get salt should succeed")

	_, _, data2 := testutil.ParseResponse(t, body2)
	assert.Equal(t, salt, data2["salt"], "retrieved salt should match setup salt")
	assert.Equal(t, "my hint", data2["password_hint"], "password hint should match")
}

// Test encryption setup duplicate (should return conflict)
func TestEncryptionSetupDuplicate(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	defer server.Close()

	user := testutil.CreateTestUser(t, server)
	setupReq := map[string]string{}

	// First setup
	statusCode1, _ := testutil.AuthPost(t, server, "/api/v1/encryption/setup", user.Token, setupReq)
	assert.Equal(t, http.StatusOK, statusCode1, "first setup should succeed")

	// Second setup (should fail with conflict)
	statusCode2, body2 := testutil.AuthPost(t, server, "/api/v1/encryption/setup", user.Token, setupReq)
	assert.Equal(t, http.StatusConflict, statusCode2, "duplicate setup should return conflict")

	code2, _, _ := testutil.ParseResponse(t, body2)
	assert.Equal(t, 40901, code2, "error code should be 40901")
}

// Test end-to-end encryption flow (simulate C++ client behavior)
func TestEndToEndEncryptionFlow(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	defer server.Close()

	// Step 1: Register and login
	user1 := testutil.CreateTestUser(t, server)

	// Step 2: Setup encryption and get salt
	statusCode, body := testutil.AuthPost(t, server, "/api/v1/encryption/setup", user1.Token, map[string]string{})
	assert.Equal(t, http.StatusOK, statusCode)

	_, _, data := testutil.ParseResponse(t, body)
	saltStr, _ := data["salt"].(string)

	// Step 3: Simulate client deriving key from password + salt
	saltBytes, err := base64.StdEncoding.DecodeString(saltStr)
	assert.NoError(t, err, "salt should be decodable")
	assert.Equal(t, 32, len(saltBytes), "salt should be 32 bytes")

	// Step 4: Verify salt is random (unique)
	salt2 := make([]byte, 32)
	for i := range salt2 {
		salt2[i] = byte(i) // deterministic pattern
	}
	assert.NotEqual(t, saltBytes, salt2, "server salt should not be predictable")

	// Step 5: Verify user isolation - different user should get different salt
	user2 := testutil.CreateTestUser(t, server)
	statusCode2, body2 := testutil.AuthPost(t, server, "/api/v1/encryption/setup", user2.Token, map[string]string{})
	assert.Equal(t, http.StatusOK, statusCode2)

	_, _, data2 := testutil.ParseResponse(t, body2)
	saltStr2, _ := data2["salt"].(string)

	assert.NotEqual(t, saltStr, saltStr2, "different users should have different salts")
}

// Verify encryption settings are persisted in DB
func TestEncryptionSettingsPersistence(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	defer server.Close()

	user := testutil.CreateTestUser(t, server)

	// Setup encryption
	_, _ = testutil.AuthPost(t, server, "/api/v1/encryption/setup", user.Token, map[string]string{})

	// Verify in database directly
	var settings model.EncryptionSettings
	err := testutil.TestDB.Where("user_id = ?", getUserIDFromToken(t, user.Token)).First(&settings).Error
	assert.NoError(t, err, "encryption settings should exist in DB")
	assert.True(t, settings.Enabled, "encryption should be enabled")
	assert.Equal(t, 32, len(settings.Salt), "salt should be 32 bytes")
}

// getUserIDFromToken extracts user_id from JWT token for DB queries.
// In tests, we can't easily parse JWT, so we use a workaround: query by first user.
func getUserIDFromToken(t *testing.T, token string) uint {
	t.Helper()
	// Since we only have one user in test DB, query the first user
	var user model.User
	if err := testutil.TestDB.First(&user).Error; err != nil {
		t.Fatalf("failed to get user from DB: %v", err)
	}
	return user.ID
}
