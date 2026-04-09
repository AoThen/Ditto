package tests

import (
	"encoding/base64"
	"fmt"
	"net/http"
	"testing"
	"time"

	"ditto-cloud-server/tests/testutil"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// TestLWW_NewerWins: device A pushes old clip, device B pushes newer clip for same ID
// The newer clip should win.
func TestLWW_NewerWins(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	defer server.Close()

	// Register user
	statusCode, _ := testutil.RegisterUser(t, server, "lwwuser", "lwwuser@example.com", "password123")
	require.Equal(t, http.StatusOK, statusCode)

	// Login device A
	statusCode, loginRespA, _ := testutil.LoginUserWithDeviceName(t, server, "lwwuser", "password123", "DeviceA")
	require.Equal(t, http.StatusOK, statusCode)
	tokenA := getDeviceToken(t, loginRespA)
	deviceA := getDeviceID(t, loginRespA)

	// Login device B
	statusCode, loginRespB, _ := testutil.LoginUserWithDeviceName(t, server, "lwwuser", "password123", "DeviceB")
	require.Equal(t, http.StatusOK, statusCode)
	tokenB := getDeviceToken(t, loginRespB)
	deviceB := getDeviceID(t, loginRespB)

	clipID := fmt.Sprintf("clip-lww-%d", time.Now().UnixNano())

	// Device A pushes an OLD clip (updated_at = 1 hour ago)
	oldTime := time.Now().Add(-time.Hour).UTC()
	pushA := buildSyncRequest(clipID, "Old content from A", deviceA, oldTime)
	statusCode, bodyA := testutil.AuthPost(t, server, "/api/v1/clips/sync", tokenA, pushA)
	require.Equal(t, http.StatusOK, statusCode)
	codeA, _, dataA := testutil.ParseResponse(t, bodyA)
	assert.Equal(t, 0, codeA)
	assert.Equal(t, float64(1), dataA["updated_count"], "device A should push 1 clip")

	// Small delay
	time.Sleep(100 * time.Millisecond)

	// Device B pushes a NEWER clip (updated_at = now) for the SAME clip ID
	newTime := time.Now().UTC()
	pushB := buildSyncRequest(clipID, "Newer content from B", deviceB, newTime)
	statusCode, bodyB := testutil.AuthPost(t, server, "/api/v1/clips/sync", tokenB, pushB)
	require.Equal(t, http.StatusOK, statusCode)
	codeB, _, dataB := testutil.ParseResponse(t, bodyB)
	assert.Equal(t, 0, codeB)
	assert.Equal(t, float64(1), dataB["updated_count"], "device B should update 1 clip (newer wins)")

	// Device A pulls: should see B's newer content
	time.Sleep(100 * time.Millisecond)
	pullA := buildPullRequest(deviceA, time.Now().Add(-2*time.Hour))
	statusCode, pullBodyA := testutil.AuthPost(t, server, "/api/v1/clips/sync", tokenA, pullA)
	require.Equal(t, http.StatusOK, statusCode)
	_, _, pullDataA := testutil.ParseResponse(t, pullBodyA)
	newClipsA := pullDataA["new_clips"].([]interface{})
	// Should receive B's clip since it's newer
	if len(newClipsA) > 0 {
		clip := newClipsA[0].(map[string]interface{})
		assert.Equal(t, "Newer content from B", clip["description"], "device A should receive B's newer content")
	}
}

// TestLWW_OlderSkipped: device pushes an older version of an existing clip, should be skipped
func TestLWW_OlderSkipped(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	defer server.Close()

	statusCode, _ := testutil.RegisterUser(t, server, "lwwuser2", "lwwuser2@example.com", "password123")
	require.Equal(t, http.StatusOK, statusCode)

	statusCode, loginResp, _ := testutil.LoginUserWithDeviceName(t, server, "lwwuser2", "password123", "DeviceX")
	require.Equal(t, http.StatusOK, statusCode)
	token := getDeviceToken(t, loginResp)
	deviceID := getDeviceID(t, loginResp)

	clipID := fmt.Sprintf("clip-lww-old-%d", time.Now().UnixNano())

	// Push a clip with current time
	firstPushTime := time.Now().UTC()
	push1 := buildSyncRequest(clipID, "First version", deviceID, firstPushTime)
	statusCode, body1 := testutil.AuthPost(t, server, "/api/v1/clips/sync", token, push1)
	require.Equal(t, http.StatusOK, statusCode)
	_, _, data1 := testutil.ParseResponse(t, body1)
	assert.Equal(t, float64(1), data1["updated_count"])

	// Push an OLDER version of the same clip (should be skipped by LWW)
	oldTime := firstPushTime.Add(-time.Hour)
	push2 := buildSyncRequest(clipID, "Older version (should be skipped)", deviceID, oldTime)
	statusCode, body2 := testutil.AuthPost(t, server, "/api/v1/clips/sync", token, push2)
	require.Equal(t, http.StatusOK, statusCode)
	_, _, data2 := testutil.ParseResponse(t, body2)

	// Should skip the older version
	assert.Equal(t, float64(0), data2["updated_count"], "older version should not update")
	assert.Equal(t, float64(1), data2["skipped_count"], "older version should be skipped")
}

// TestLWW_SameTime: two pushes with same updated_at, second should be skipped (tie-breaking)
func TestLWW_SameTime(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	defer server.Close()

	statusCode, _ := testutil.RegisterUser(t, server, "lwwuser3", "lwwuser3@example.com", "password123")
	require.Equal(t, http.StatusOK, statusCode)

	statusCode, loginResp, _ := testutil.LoginUserWithDeviceName(t, server, "lwwuser3", "password123", "DeviceY")
	require.Equal(t, http.StatusOK, statusCode)
	token := getDeviceToken(t, loginResp)
	deviceID := getDeviceID(t, loginResp)

	clipID := fmt.Sprintf("clip-lww-same-%d", time.Now().UnixNano())
	sameTime := time.Now().UTC()

	// First push
	push1 := buildSyncRequest(clipID, "First push", deviceID, sameTime)
	statusCode, body1 := testutil.AuthPost(t, server, "/api/v1/clips/sync", token, push1)
	require.Equal(t, http.StatusOK, statusCode)
	_, _, data1 := testutil.ParseResponse(t, body1)
	assert.Equal(t, float64(1), data1["updated_count"])

	// Second push with same timestamp (should be skipped as tie)
	push2 := buildSyncRequest(clipID, "Second push (same time)", deviceID, sameTime)
	statusCode, body2 := testutil.AuthPost(t, server, "/api/v1/clips/sync", token, push2)
	require.Equal(t, http.StatusOK, statusCode)
	_, _, data2 := testutil.ParseResponse(t, body2)

	// Same time = not newer, so should be skipped
	assert.Equal(t, float64(0), data2["updated_count"], "same-time push should not update")
	assert.Equal(t, float64(1), data2["skipped_count"], "same-time push should be skipped")
}

// Helper functions

func getDeviceToken(t *testing.T, respBody []byte) string {
	t.Helper()
	_, _, data := testutil.ParseResponse(t, respBody)
	return data["device_token"].(string)
}

func getDeviceID(t *testing.T, respBody []byte) string {
	t.Helper()
	_, _, data := testutil.ParseResponse(t, respBody)
	return data["device_id"].(string)
}

func buildSyncRequest(clipID, description, deviceID string, updatedAt time.Time) map[string]interface{} {
	return map[string]interface{}{
		"since":     "1970-01-01T00:00:00Z",
		"device_id": deviceID,
		"push_clips": []map[string]interface{}{
			{
				"id":          clipID,
				"description": description,
				"crc":         0,
				"group_id":    "",
				"short_cut":   0,
				"updated_at":  updatedAt.Format(time.RFC3339),
				"formats": []map[string]interface{}{
					{
						"format_type": 13,
						"data":        base64.StdEncoding.EncodeToString([]byte(description)),
					},
				},
			},
		},
	}
}

func buildPullRequest(deviceID string, since time.Time) map[string]interface{} {
	return map[string]interface{}{
		"since":      since.UTC().Format(time.RFC3339),
		"device_id":  deviceID,
		"push_clips": []map[string]interface{}{},
	}
}
