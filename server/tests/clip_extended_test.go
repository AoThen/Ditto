package tests

import (
	"encoding/base64"
	"encoding/json"
	"fmt"
	"net/http"
	"testing"
	"time"

	"ditto-cloud-server/tests/testutil"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

func TestDownloadClip_Success(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	user := testutil.CreateTestUser(t, server)

	clipID := fmt.Sprintf("clip-dl-%d", time.Now().UnixNano())
	createClipViaSync(t, server, user.Token, user.DeviceID, clipID, "Download test", "Download content")

	statusCode, respBody := testutil.AuthGet(t, server, "/api/v1/clips/"+clipID+"/download", user.Token)
	assert.Equal(t, http.StatusOK, statusCode)
	assert.NotEmpty(t, respBody)

	assert.Equal(t, "Download content", string(respBody))
}

func TestDownloadClip_DifferentFormat(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	user := testutil.CreateTestUser(t, server)

	clipID := fmt.Sprintf("clip-dl-fmt-%d", time.Now().UnixNano())
	htmlContent := "<html><body>Hello</body></html>"
	syncBody := map[string]interface{}{
		"since":     "2000-01-01T00:00:00Z",
		"device_id": user.DeviceID,
		"push_clips": []map[string]interface{}{
			{
				"id":          clipID,
				"description": "HTML clip",
				"crc":         0,
				"group_id":    "",
				"short_cut":   0,
				"formats": []map[string]interface{}{
					{
						"format_type": 49,
						"data":        base64.StdEncoding.EncodeToString([]byte(htmlContent)),
					},
				},
			},
		},
	}
	statusCode, _ := testutil.AuthPost(t, server, "/api/v1/clips/sync", user.Token, syncBody)
	require.Equal(t, http.StatusOK, statusCode)

	statusCode, respBody := testutil.AuthGet(t, server, "/api/v1/clips/"+clipID+"/download?format_type=49", user.Token)
	assert.Equal(t, http.StatusOK, statusCode)
	assert.Equal(t, htmlContent, string(respBody))
}

func TestDownloadClip_NotFound(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	user := testutil.CreateTestUser(t, server)

	statusCode, _ := testutil.AuthGet(t, server, "/api/v1/clips/non-existent/download", user.Token)
	assert.Equal(t, http.StatusNotFound, statusCode)
}

func TestListConflictClips_Empty(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	user := testutil.CreateTestUser(t, server)

	statusCode, respBody := testutil.AuthGet(t, server, "/api/v1/clips/conflicts", user.Token)
	assert.Equal(t, http.StatusOK, statusCode)
	code, _, _ := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)

	// ListConflictClips returns data as paginated response
	var respMap map[string]interface{}
	json.Unmarshal(respBody, &respMap)
	items, ok := getGroupItems(respMap["data"])
	require.True(t, ok)
	assert.Empty(t, items)
}

func TestGetChanges_PullOnly(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	user := testutil.CreateTestUser(t, server)

	clipID := fmt.Sprintf("clip-chg-%d", time.Now().UnixNano())
	createClipViaSync(t, server, user.Token, user.DeviceID, clipID, "Changes clip", "Changes content")

	since := time.Now().Add(-time.Hour).UTC().Format(time.RFC3339)
	statusCode, respBody := testutil.AuthGet(t, server, "/api/v1/clips/changes?since="+since, user.Token)
	assert.Equal(t, http.StatusOK, statusCode)
	code, _, data := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)
	assert.NotNil(t, data["clips"])
	assert.NotNil(t, data["server_time"])
	assert.NotNil(t, data["has_more"])
	assert.NotNil(t, data["deleted_ids"])
}

// TestGetChanges_CrossDeviceDeletion — create on device A, delete via device B,
// then verify device A learns about the deletion in deleted_ids.
func TestGetChanges_CrossDeviceDeletion(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	// Register one user, then obtain tokens for two separate devices.
	statusCode, _ := testutil.RegisterUser(t, server, "crossdeluser", "crossdel@test.com", "password123")
	require.Equal(t, http.StatusOK, statusCode)

	loginA, loginABody, _ := testutil.LoginUserWithDeviceName(t, server, "crossdeluser", "password123", "device-a")
	require.Equal(t, http.StatusOK, loginA)
	_, _, dataA := testutil.ParseResponse(t, loginABody)
	tokenA, _ := dataA["device_token"].(string)
	devA, _ := dataA["device_id"].(string)
	require.NotEmpty(t, tokenA)

	loginB, loginBBody, _ := testutil.LoginUserWithDeviceName(t, server, "crossdeluser", "password123", "device-b")
	require.Equal(t, http.StatusOK, loginB)
	_, _, dataB := testutil.ParseResponse(t, loginBBody)
	tokenB, _ := dataB["device_token"].(string)
	require.NotEmpty(t, tokenB)

	clipID := fmt.Sprintf("clip-crossdel-%d", time.Now().UnixNano())
	createClipViaSync(t, server, tokenA, devA, clipID, "Cross-device delete", "to be deleted by B")

	// Device B deletes the clip created by device A
	delStatus, delBody := testutil.AuthDelete(t, server, "/api/v1/clips/"+clipID, tokenB)
	require.Equal(t, http.StatusOK, delStatus, "delete failed: %s", string(delBody))

	// Device A pulls changes — must learn about the deletion
	since := time.Now().Add(-time.Hour).UTC().Format(time.RFC3339)
	statusCode, respBody := testutil.AuthGet(t, server, "/api/v1/clips/changes?since="+since, tokenA)
	require.Equal(t, http.StatusOK, statusCode)
	code, _, data := testutil.ParseResponse(t, respBody)
	require.Equal(t, 0, code)

	deleted, ok := data["deleted_ids"].([]interface{})
	require.True(t, ok, "deleted_ids should be a list")
	assert.Contains(t, deleted, clipID, "creator device should be notified of the deletion")
}

func TestGetChanges_EmptySince(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	user := testutil.CreateTestUser(t, server)

	statusCode, respBody := testutil.AuthGet(t, server, "/api/v1/clips/changes", user.Token)
	assert.Equal(t, http.StatusOK, statusCode)
	code, _, data := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)
	assert.NotNil(t, data["clips"])
}

func TestGetChanges_InvalidSince(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	user := testutil.CreateTestUser(t, server)

	statusCode, _ := testutil.AuthGet(t, server, "/api/v1/clips/changes?since=invalid-date", user.Token)
	assert.Equal(t, http.StatusBadRequest, statusCode)
}

func TestResolveConflictClip_InvalidAction(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	user := testutil.CreateTestUser(t, server)

	body := map[string]string{"action": "invalid"}
	statusCode, _ := testutil.AuthPost(t, server, "/api/v1/clips/conflicts/nonexistent/resolve", user.Token, body)
	assert.Equal(t, http.StatusBadRequest, statusCode)
}

func TestResolveConflictClip_NotFound(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	user := testutil.CreateTestUser(t, server)

	body := map[string]string{"action": "discard"}
	statusCode, _ := testutil.AuthPost(t, server, "/api/v1/clips/conflicts/nonexistent/resolve", user.Token, body)
	assert.Equal(t, http.StatusNotFound, statusCode)
}
