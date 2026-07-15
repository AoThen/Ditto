package testutil

import (
	"encoding/base64"
	"fmt"
	"net/http/httptest"
	"testing"
	"time"
)

func uniqueID() string {
	return fmt.Sprintf("clip-%d", time.Now().UnixNano())
}

type TestUser struct {
	Username string
	Email    string
	Password string
	Token    string
	DeviceID string
}

// CreateFirstUser registers and logs in the first user (becomes admin).
func CreateFirstUser(t *testing.T, server *httptest.Server) *TestUser {
	t.Helper()
	uid := uniqueID()[:16]
	username := fmt.Sprintf("testuser_%s", uid)
	email := fmt.Sprintf("%s@example.com", username)
	password := "password123"

	statusCode, _ := RegisterUser(t, server, username, email, password)
	if statusCode != 200 {
		t.Fatalf("failed to register first user: status=%d", statusCode)
	}

	token, deviceID := loginAndExtract(t, server, username, password)
	return &TestUser{
		Username: username, Email: email, Password: password,
		Token: token, DeviceID: deviceID,
	}
}

// CreateTestUser is an alias for CreateFirstUser (backward compat for single-user tests).
func CreateTestUser(t *testing.T, server *httptest.Server) *TestUser {
	return CreateFirstUser(t, server)
}

// CreateUserViaAdmin creates a user using the admin API and logs in.
// adminToken must belong to an admin user.
func CreateUserViaAdmin(t *testing.T, server *httptest.Server, adminToken, username, email, password string) *TestUser {
	t.Helper()

	statusCode, _ := AuthPost(t, server, "/api/v1/admin/users", adminToken, map[string]string{
		"username": username,
		"email":    email,
		"password": password,
	})
	if statusCode != 200 {
		t.Fatalf("failed to create user via admin API: status=%d", statusCode)
	}

	token, deviceID := loginAndExtract(t, server, username, password)
	return &TestUser{
		Username: username, Email: email, Password: password,
		Token: token, DeviceID: deviceID,
	}
}

// loginAndExtract logs in a user and extracts the token (from cookie or body).
func loginAndExtract(t *testing.T, server *httptest.Server, username, password string) (string, string) {
	t.Helper()
	statusCode, respBody, setCookies := LoginUserWithCookies(t, server, username, password)
	if statusCode != 200 {
		return "", ""
	}

	token := ExtractCookie(setCookies, "device_token")
	if token == "" {
		_, _, data := ParseResponse(t, respBody)
		token, _ = data["device_token"].(string)
	}

	_, _, data := ParseResponse(t, respBody)
	deviceID, _ := data["device_id"].(string)

	return token, deviceID
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

type FormatPayload struct {
	FormatType int
	Data       string
}

func TextFormat(text string) FormatPayload {
	return FormatPayload{
		FormatType: 13,
		Data:       base64.StdEncoding.EncodeToString([]byte(text)),
	}
}

func HTMLFormat(html string) FormatPayload {
	return FormatPayload{
		FormatType: 49,
		Data:       base64.StdEncoding.EncodeToString([]byte(html)),
	}
}

func SyncPayload(since string, deviceID string, pushClips []map[string]interface{}) map[string]interface{} {
	return map[string]interface{}{
		"since":      since,
		"device_id":  deviceID,
		"push_clips": pushClips,
	}
}
