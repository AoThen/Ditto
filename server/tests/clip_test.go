package tests

import (
	"encoding/base64"
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"

	"ditto-cloud-server/tests/testutil"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// createClipViaSync creates a clip by pushing it through the sync endpoint.
func createClipViaSync(t *testing.T, server *httptest.Server, token, deviceID, clipID, description string, textContent string) {
	t.Helper()

	syncBody := map[string]interface{}{
		"since":     "2000-01-01T00:00:00Z",
		"device_id": deviceID,
		"push_clips": []map[string]interface{}{
			{
				"id":          clipID,
				"description": description,
				"crc":         0,
				"group_id":    "",
				"short_cut":   0,
				"formats": []map[string]interface{}{
					{
						"format_type": 13,
						"data":        base64.StdEncoding.EncodeToString([]byte(textContent)),
					},
				},
			},
		},
	}

	statusCode, respBody := testutil.AuthPost(t, server, "/api/v1/clips/sync", token, syncBody)
	require.Equal(t, http.StatusOK, statusCode, "sync push failed: %s", string(respBody))
	code, _, _ := testutil.ParseResponse(t, respBody)
	require.Equal(t, 0, code)
}

// TestCreateClip_Success — push a clip via sync, expect code=0
func TestCreateClip_Success(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	user := testutil.CreateTestUser(t, server)

	clipID := fmt.Sprintf("clip-%d", time.Now().UnixNano())
	syncBody := map[string]interface{}{
		"since":     "2000-01-01T00:00:00Z",
		"device_id": user.DeviceID,
		"push_clips": []map[string]interface{}{
			{
				"id":          clipID,
				"description": "Test clip",
				"crc":         12345,
				"group_id":    "",
				"short_cut":   0,
				"formats": []map[string]interface{}{
					{
						"format_type": 13,
						"data":        base64.StdEncoding.EncodeToString([]byte("Hello, World!")),
					},
				},
			},
		},
	}

	statusCode, respBody := testutil.AuthPost(t, server, "/api/v1/clips/sync", user.Token, syncBody)
	assert.Equal(t, http.StatusOK, statusCode)
	code, _, data := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)
	assert.NotNil(t, data)

	// Verify the clip was created by listing clips
	statusCode, respBody = testutil.AuthGet(t, server, "/api/v1/clips", user.Token)
	require.Equal(t, http.StatusOK, statusCode)
	code, _, data = testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)

	items, ok := data["items"].([]interface{})
	require.True(t, ok)
	assert.Len(t, items, 1)

	clipItem := items[0].(map[string]interface{})
	assert.Equal(t, clipID, clipItem["id"])
	assert.Equal(t, "Test clip", clipItem["description"])
}

// TestListClips_Pagination — create 25 clips, request page=1 per_page=20, expect 20 items
func TestListClips_Pagination(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	user := testutil.CreateTestUser(t, server)

	// Create 25 clips
	for i := 0; i < 25; i++ {
		clipID := fmt.Sprintf("clip-pag-%d-%d", time.Now().UnixNano(), i)
		createClipViaSync(t, server, user.Token, user.DeviceID, clipID, fmt.Sprintf("Clip %d", i), fmt.Sprintf("Content %d", i))
	}

	// Request first page with 20 per page
	statusCode, respBody := testutil.AuthGet(t, server, "/api/v1/clips?page=1&per_page=20", user.Token)
	require.Equal(t, http.StatusOK, statusCode)
	code, _, data := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)

	total := int(data["total"].(float64))
	page := int(data["page"].(float64))
	perPage := int(data["per_page"].(float64))
	items := data["items"].([]interface{})

	assert.Equal(t, 25, total)
	assert.Equal(t, 1, page)
	assert.Equal(t, 20, perPage)
	assert.Len(t, items, 20)

	// Request second page
	statusCode, respBody = testutil.AuthGet(t, server, "/api/v1/clips?page=2&per_page=20", user.Token)
	require.Equal(t, http.StatusOK, statusCode)
	code, _, data = testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)

	items = data["items"].([]interface{})
	assert.Len(t, items, 5)
}

// TestListClips_Search — create clips with different descriptions, search by keyword
func TestListClips_Search(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	user := testutil.CreateTestUser(t, server)

	// Create clips with different descriptions
	createClipViaSync(t, server, user.Token, user.DeviceID,
		fmt.Sprintf("clip-search-%d-1", time.Now().UnixNano()),
		"Important meeting notes", "Meeting content")
	createClipViaSync(t, server, user.Token, user.DeviceID,
		fmt.Sprintf("clip-search-%d-2", time.Now().UnixNano()),
		"Random text snippet", "Random content")
	createClipViaSync(t, server, user.Token, user.DeviceID,
		fmt.Sprintf("clip-search-%d-3", time.Now().UnixNano()),
		"Important password", "Password content")

	// Search for "Important"
	statusCode, respBody := testutil.AuthGet(t, server, "/api/v1/clips?search=Important", user.Token)
	require.Equal(t, http.StatusOK, statusCode)
	code, _, data := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)

	items := data["items"].([]interface{})
	assert.Len(t, items, 2)

	// Search for "password"
	statusCode, respBody = testutil.AuthGet(t, server, "/api/v1/clips?search=password", user.Token)
	require.Equal(t, http.StatusOK, statusCode)
	code, _, data = testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)

	items = data["items"].([]interface{})
	assert.Len(t, items, 1)
}

// TestGetClip_Detail — get single clip, expect full format data as base64
func TestGetClip_Detail(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	user := testutil.CreateTestUser(t, server)

	clipID := fmt.Sprintf("clip-detail-%d", time.Now().UnixNano())
	textContent := "This is detailed clip content"
	createClipViaSync(t, server, user.Token, user.DeviceID, clipID, "Detail test clip", textContent)

	// Get the clip detail
	statusCode, respBody := testutil.AuthGet(t, server, "/api/v1/clips/"+clipID, user.Token)
	require.Equal(t, http.StatusOK, statusCode)
	code, _, data := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)

	assert.Equal(t, clipID, data["id"])
	assert.Equal(t, "Detail test clip", data["description"])

	// Check formats
	formats, ok := data["formats"].([]interface{})
	require.True(t, ok)
	require.Len(t, formats, 1)

	format := formats[0].(map[string]interface{})
	assert.Equal(t, float64(13), format["format_type"])

	// Verify the data is base64-encoded and decodes correctly
	dataStr, _ := format["data"].(string)
	decoded, err := base64.StdEncoding.DecodeString(dataStr)
	require.NoError(t, err)
	assert.Equal(t, textContent, string(decoded))
}

// TestDeleteClip_Success — delete a clip, then verify it's gone
func TestDeleteClip_Success(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	user := testutil.CreateTestUser(t, server)

	clipID := fmt.Sprintf("clip-delete-%d", time.Now().UnixNano())
	createClipViaSync(t, server, user.Token, user.DeviceID, clipID, "To be deleted", "Delete me")

	// Verify clip exists
	statusCode, respBody := testutil.AuthGet(t, server, "/api/v1/clips/"+clipID, user.Token)
	require.Equal(t, http.StatusOK, statusCode)

	// Delete the clip
	statusCode, respBody = testutil.AuthDelete(t, server, "/api/v1/clips/"+clipID, user.Token)
	assert.Equal(t, http.StatusOK, statusCode)
	code, message, _ := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)
	assert.Equal(t, "剪贴板已删除", message)

	// Verify clip is gone
	statusCode, respBody = testutil.AuthGet(t, server, "/api/v1/clips/"+clipID, user.Token)
	assert.Equal(t, http.StatusNotFound, statusCode)
	code, _, _ = testutil.ParseResponse(t, respBody)
	assert.Equal(t, 40400, code)
}

// TestClip_UserIsolation — user A cannot see user B's clips
func TestClip_UserIsolation(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	admin := testutil.CreateFirstUser(t, server)
	userA := testutil.CreateUserViaAdmin(t, server, admin.Token, "userA", "userA@example.com", "password123")
	userB := testutil.CreateUserViaAdmin(t, server, admin.Token, "userB", "userB@example.com", "password123")

	// User A creates a clip
	clipIDA := fmt.Sprintf("clip-isolation-%d", time.Now().UnixNano())
	createClipViaSync(t, server, userA.Token, userA.DeviceID, clipIDA, "User A's secret", "Secret content")

	// User A should see their clip
	statusCode, respBody := testutil.AuthGet(t, server, "/api/v1/clips", userA.Token)
	require.Equal(t, http.StatusOK, statusCode)
	code, _, data := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)
	itemsA := data["items"].([]interface{})
	assert.Len(t, itemsA, 1)

	// User B should NOT see user A's clip
	statusCode, respBody = testutil.AuthGet(t, server, "/api/v1/clips", userB.Token)
	require.Equal(t, http.StatusOK, statusCode)
	code, _, data = testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)
	itemsB := data["items"].([]interface{})
	assert.Len(t, itemsB, 0)

	// User B cannot access user A's clip by ID
	statusCode, _ = testutil.AuthGet(t, server, "/api/v1/clips/"+clipIDA, userB.Token)
	assert.Equal(t, http.StatusNotFound, statusCode)
}

// Helper to parse clip list response
func parseClipList(t *testing.T, respBody []byte) (code int, items []map[string]interface{}, total int64) {
	t.Helper()
	code, _, data := testutil.ParseResponse(t, respBody)

	if data == nil {
		return code, nil, 0
	}

	if itemsRaw, ok := data["items"]; ok && itemsRaw != nil {
		itemsJSON, _ := json.Marshal(itemsRaw)
		json.Unmarshal(itemsJSON, &items)
	}

	if totalRaw, ok := data["total"]; ok {
		total = int64(totalRaw.(float64))
	}

	return code, items, total
}

// TestBatchMarkDontSync_Success — mark clip as dont-sync, verify formats cleared and dont_sync=true
func TestBatchMarkDontSync_Success(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	user := testutil.CreateTestUser(t, server)

	clipID := fmt.Sprintf("clip-dontsync-%d", time.Now().UnixNano())
	createClipViaSync(t, server, user.Token, user.DeviceID, clipID, "To be unsynced", "Secret content")

	// Verify clip has formats
	statusCode, respBody := testutil.AuthGet(t, server, "/api/v1/clips/"+clipID, user.Token)
	require.Equal(t, http.StatusOK, statusCode)
	code, _, data := testutil.ParseResponse(t, respBody)
	require.Equal(t, 0, code)
	formats, ok := data["formats"].([]interface{})
	require.True(t, ok)
	require.Len(t, formats, 1, "clip should have 1 format before dont-sync")

	// Mark as dont-sync
	statusCode, respBody = testutil.AuthPost(t, server, "/api/v1/clips/batch-dont-sync", user.Token,
		map[string]interface{}{"ids": []string{clipID}})
	require.Equal(t, http.StatusOK, statusCode)
	code, _, data = testutil.ParseResponse(t, respBody)
	require.Equal(t, 0, code)
	marked, ok := data["marked"].(float64)
	require.True(t, ok)
	assert.Equal(t, float64(1), marked)

	// Verify formats are cleared
	statusCode, respBody = testutil.AuthGet(t, server, "/api/v1/clips/"+clipID, user.Token)
	require.Equal(t, http.StatusOK, statusCode)
	code, _, data = testutil.ParseResponse(t, respBody)
	require.Equal(t, 0, code)

	formats, ok = data["formats"].([]interface{})
	require.True(t, ok)
	assert.Empty(t, formats, "formats should be cleared after dont-sync")

	// Verify clip still exists (not deleted) by checking it appears in list
	statusCode, respBody = testutil.AuthGet(t, server, "/api/v1/clips", user.Token)
	require.Equal(t, http.StatusOK, statusCode)
	code, _, data = testutil.ParseResponse(t, respBody)
	require.Equal(t, 0, code)
	items, ok := data["items"].([]interface{})
	require.True(t, ok)
	found := false
	for _, item := range items {
		if m, ok := item.(map[string]interface{}); ok && m["id"] == clipID {
			found = true
			break
		}
	}
	assert.True(t, found, "clip should still exist in list after dont-sync")
}
