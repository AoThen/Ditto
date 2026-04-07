package testutil

import (
	"encoding/base64"
	"fmt"
	"net/http/httptest"
	"testing"
	"time"
)

// uniqueID generates a unique identifier based on the current timestamp.
func uniqueID() string {
	return fmt.Sprintf("clip-%d", time.Now().UnixNano())
}

// TestUser represents a fully registered and logged-in test user.
type TestUser struct {
	Username string
	Email    string
	Password string
	Token    string
	DeviceID string
}

// CreateTestUser registers and logs in a user with auto-generated credentials.
func CreateTestUser(t *testing.T, server *httptest.Server) *TestUser {
	t.Helper()
	uid := uniqueID()[:16]
	username := fmt.Sprintf("testuser_%s", uid)
	email := fmt.Sprintf("%s@example.com", username)
	password := "password123"

	token, deviceID := RegisterAndLogin(t, server, username, email, password)

	return &TestUser{
		Username: username,
		Email:    email,
		Password: password,
		Token:    token,
		DeviceID: deviceID,
	}
}

// CreateTestUserWithCreds registers and logs in a user with the given credentials.
func CreateTestUserWithCreds(t *testing.T, server *httptest.Server, username, email, password string) *TestUser {
	t.Helper()
	token, deviceID := RegisterAndLogin(t, server, username, email, password)

	return &TestUser{
		Username: username,
		Email:    email,
		Password: password,
		Token:    token,
		DeviceID: deviceID,
	}
}

// CreateClipPayload builds a clip creation request payload.
func CreateClipPayload(id, description string, formats []FormatPayload) map[string]interface{} {
	formatsJSON := make([]map[string]interface{}, len(formats))
	for i, f := range formats {
		formatsJSON[i] = map[string]interface{}{
			"format_type": f.FormatType,
			"data":        f.Data,
		}
	}

	return map[string]interface{}{
		"id":          id,
		"description": description,
		"crc":         0,
		"group_id":    "",
		"short_cut":   0,
		"formats":     formatsJSON,
	}
}

// FormatPayload represents a single format entry for a clip.
type FormatPayload struct {
	FormatType int
	Data       string // base64-encoded
}

// TextFormat creates a text format payload from a plain string.
func TextFormat(text string) FormatPayload {
	return FormatPayload{
		FormatType: 13, // CF_UNICODETEXT
		Data:       base64.StdEncoding.EncodeToString([]byte(text)),
	}
}

// HTMLFormat creates an HTML format payload from a plain string.
func HTMLFormat(html string) FormatPayload {
	return FormatPayload{
		FormatType: 49, // Custom HTML
		Data:       base64.StdEncoding.EncodeToString([]byte(html)),
	}
}

// SyncPayload builds a sync request payload.
func SyncPayload(since string, deviceID string, pushClips []map[string]interface{}) map[string]interface{} {
	return map[string]interface{}{
		"since":      since,
		"device_id":  deviceID,
		"push_clips": pushClips,
	}
}
