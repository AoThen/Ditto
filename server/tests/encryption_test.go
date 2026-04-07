package tests

import (
	"net/http"
	"testing"

	"ditto-cloud-server/tests/testutil"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// TestEncryption_Setup — setup encryption, expect salt returned
func TestEncryption_Setup(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	user := testutil.CreateTestUser(t, server)

	// Setup encryption
	setupBody := map[string]string{
		"password_hint": "my favorite color",
	}

	statusCode, respBody := testutil.AuthPost(t, server, "/api/v1/encryption/setup", user.Token, setupBody)
	assert.Equal(t, http.StatusOK, statusCode)
	code, message, data := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)
	assert.Equal(t, "加密已启用", message)
	assert.NotNil(t, data)

	// Verify salt is returned
	salt, ok := data["salt"].(string)
	require.True(t, ok, "response should contain salt")
	assert.NotEmpty(t, salt, "salt should not be empty")

	enabled, ok := data["encryption_enabled"].(bool)
	require.True(t, ok, "response should contain encryption_enabled")
	assert.True(t, enabled, "encryption should be enabled")
}

// TestEncryption_GetSalt — after setup, get salt, expect same salt
func TestEncryption_GetSalt(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	user := testutil.CreateTestUser(t, server)

	// Setup encryption first
	setupBody := map[string]string{
		"password_hint": "my hint",
	}
	statusCode, respBody := testutil.AuthPost(t, server, "/api/v1/encryption/setup", user.Token, setupBody)
	require.Equal(t, http.StatusOK, statusCode)
	code, _, setupData := testutil.ParseResponse(t, respBody)
	require.Equal(t, 0, code)

	expectedSalt := setupData["salt"].(string)

	// Get the salt
	statusCode, respBody = testutil.AuthGet(t, server, "/api/v1/encryption/salt", user.Token)
	assert.Equal(t, http.StatusOK, statusCode)
	code, _, getData := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)

	// Verify salt matches
	actualSalt := getData["salt"].(string)
	assert.Equal(t, expectedSalt, actualSalt, "salt should be the same after setup")

	// Verify other fields
	enabled, ok := getData["encryption_enabled"].(bool)
	require.True(t, ok)
	assert.True(t, enabled)

	hint, ok := getData["password_hint"].(string)
	require.True(t, ok)
	assert.Equal(t, "my hint", hint)
}

// TestEncryption_DuplicateSetup — setup twice, expect code=40901
func TestEncryption_DuplicateSetup(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	user := testutil.CreateTestUser(t, server)

	// First setup should succeed
	setupBody := map[string]string{
		"password_hint": "first hint",
	}
	statusCode, respBody := testutil.AuthPost(t, server, "/api/v1/encryption/setup", user.Token, setupBody)
	require.Equal(t, http.StatusOK, statusCode)
	code, _, setupData := testutil.ParseResponse(t, respBody)
	require.Equal(t, 0, code)
	firstSalt := setupData["salt"].(string)

	// Second setup should fail with conflict
	statusCode, respBody = testutil.AuthPost(t, server, "/api/v1/encryption/setup", user.Token, map[string]string{
		"password_hint": "second hint",
	})
	assert.Equal(t, http.StatusConflict, statusCode)
	code, message, _ := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 40901, code)
	assert.Contains(t, message, "加密已启用")

	// Verify the salt hasn't changed
	statusCode, respBody = testutil.AuthGet(t, server, "/api/v1/encryption/salt", user.Token)
	require.Equal(t, http.StatusOK, statusCode)
	_, _, getData := testutil.ParseResponse(t, respBody)
	assert.Equal(t, firstSalt, getData["salt"].(string))
}
