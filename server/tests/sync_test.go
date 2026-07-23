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

// getTokenFromLoginResp extracts the device_token from a raw login response body.
func getTokenFromLoginResp(t *testing.T, respBody []byte) string {
	t.Helper()
	_, _, data := testutil.ParseResponse(t, respBody)
	token, ok := data["device_token"].(string)
	if !ok {
		t.Fatal("login response missing device_token")
	}
	return token
}

// getDeviceIDFromLoginResp extracts the device_id from a raw login response body.
func getDeviceIDFromLoginResp(t *testing.T, respBody []byte) string {
	t.Helper()
	_, _, data := testutil.ParseResponse(t, respBody)
	deviceID, ok := data["device_id"].(string)
	if !ok {
		t.Fatal("login response missing device_id")
	}
	return deviceID
}

// TestSync_PushAndPull — device A pushes clip, device B pulls it via sync
func TestSync_PushAndPull(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	// Register user
	statusCode, respBody := testutil.RegisterUser(t, server, "syncuser", "syncuser@example.com", "password123")
	require.Equal(t, http.StatusOK, statusCode)
	code, _, _ := testutil.ParseResponse(t, respBody)
	require.Equal(t, 0, code)

	// Login device A
	statusCode, loginRespA, _ := testutil.LoginUserWithDeviceName(t, server, "syncuser", "password123", "DeviceA")
	require.Equal(t, http.StatusOK, statusCode)
	tokenA := getTokenFromLoginResp(t, loginRespA)
	deviceA := getDeviceIDFromLoginResp(t, loginRespA)

	// Login device B
	statusCode, loginRespB, _ := testutil.LoginUserWithDeviceName(t, server, "syncuser", "password123", "DeviceB")
	require.Equal(t, http.StatusOK, statusCode)
	tokenB := getTokenFromLoginResp(t, loginRespB)
	deviceB := getDeviceIDFromLoginResp(t, loginRespB)

	require.NotEqual(t, deviceA, deviceB, "device A and B should have different IDs")

	// Device A pushes a clip
	clipID := fmt.Sprintf("clip-sync-%d", time.Now().UnixNano())
	syncBody := map[string]interface{}{
		"since":     "2000-01-01T00:00:00Z",
		"device_id": deviceA,
		"push_clips": []map[string]interface{}{
			{
				"id":          clipID,
				"description": "Clip from DeviceA",
				"crc":         12345,
				"group_id":    "",
				"short_cut":   0,
				"formats": []map[string]interface{}{
					{
						"format_type": 13,
						"data":        base64.StdEncoding.EncodeToString([]byte("Synced content")),
					},
				},
			},
		},
	}

	statusCode, respBody = testutil.AuthPost(t, server, "/api/v1/clips/sync", tokenA, syncBody)
	require.Equal(t, http.StatusOK, statusCode, "device A push failed: %s", string(respBody))
	code, _, data := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)
	assert.NotNil(t, data)

	// Device B pulls via sync
	sinceTime := time.Now().Add(-time.Minute).UTC().Format(time.RFC3339)
	pullBody := map[string]interface{}{
		"since":      sinceTime,
		"device_id":  deviceB,
		"push_clips": []map[string]interface{}{},
	}

	statusCode, respBody = testutil.AuthPost(t, server, "/api/v1/clips/sync", tokenB, pullBody)
	require.Equal(t, http.StatusOK, statusCode, "device B pull failed: %s", string(respBody))
	code, _, pullData := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)

	// Device B should receive the clip from Device A
	newClips, ok := pullData["new_clips"].([]interface{})
	require.True(t, ok, "response should have new_clips array")
	require.Len(t, newClips, 1, "device B should receive 1 clip from device A")

	receivedClip := newClips[0].(map[string]interface{})
	assert.Equal(t, clipID, receivedClip["id"])
	assert.Equal(t, "Clip from DeviceA", receivedClip["description"])

	// Verify formats are included with base64-encoded data for the pulling device
	formats, ok := receivedClip["formats"].([]interface{})
	require.True(t, ok)
	require.Len(t, formats, 1)

	format := formats[0].(map[string]interface{})
	assert.Equal(t, float64(13), format["format_type"])
	// Sync response includes format data (base64-encoded) for the pulling device
	formatData, dataPresent := format["data"]
	assert.True(t, dataPresent, "sync response should include format data")
	assert.Equal(t, base64.StdEncoding.EncodeToString([]byte("Synced content")), formatData.(string))
}

// TestSync_SameDevice — device A syncs, should NOT get its own clips back
func TestSync_SameDevice(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	// Register and login
	testutil.RegisterUser(t, server, "samedevice", "samedevice@example.com", "password123")
	statusCode, loginResp, _ := testutil.LoginUserWithDeviceName(t, server, "samedevice", "password123", "OnlyDevice")
	require.Equal(t, http.StatusOK, statusCode)
	token := getTokenFromLoginResp(t, loginResp)
	deviceID := getDeviceIDFromLoginResp(t, loginResp)

	// Push a clip
	clipID := fmt.Sprintf("clip-samedev-%d", time.Now().UnixNano())
	syncBody := map[string]interface{}{
		"since":     "2000-01-01T00:00:00Z",
		"device_id": deviceID,
		"push_clips": []map[string]interface{}{
			{
				"id":          clipID,
				"description": "My own clip",
				"crc":         0,
				"group_id":    "",
				"short_cut":   0,
				"formats": []map[string]interface{}{
					{
						"format_type": 13,
						"data":        base64.StdEncoding.EncodeToString([]byte("Own content")),
					},
				},
			},
		},
	}

	statusCode, respBody := testutil.AuthPost(t, server, "/api/v1/clips/sync", token, syncBody)
	require.Equal(t, http.StatusOK, statusCode)
	code, _, _ := testutil.ParseResponse(t, respBody)
	require.Equal(t, 0, code)

	// Sync again from the same device — should NOT get its own clip back
	sinceTime := time.Now().Add(-time.Minute).UTC().Format(time.RFC3339)
	syncAgain := map[string]interface{}{
		"since":      sinceTime,
		"device_id":  deviceID,
		"push_clips": []map[string]interface{}{},
	}

	statusCode, respBody = testutil.AuthPost(t, server, "/api/v1/clips/sync", token, syncAgain)
	require.Equal(t, http.StatusOK, statusCode)
	code, _, data := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)

	// Should NOT receive any clips (they're from the same device)
	newClips, ok := data["new_clips"].([]interface{})
	require.True(t, ok)
	assert.Len(t, newClips, 0, "same device should not receive its own clips")
}

// TestSync_EmptyPush — sync with empty push, should still return changes
func TestSync_EmptyPush(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	// Register user
	testutil.RegisterUser(t, server, "emptysync", "emptysync@example.com", "password123")

	// Login device A and push a clip
	statusCode, loginRespA, _ := testutil.LoginUserWithDeviceName(t, server, "emptysync", "password123", "PushDevice")
	require.Equal(t, http.StatusOK, statusCode)
	tokenA := getTokenFromLoginResp(t, loginRespA)
	deviceA := getDeviceIDFromLoginResp(t, loginRespA)

	// Push a clip from device A
	clipID := fmt.Sprintf("clip-emptypush-%d", time.Now().UnixNano())
	syncBody := map[string]interface{}{
		"since":     "2000-01-01T00:00:00Z",
		"device_id": deviceA,
		"push_clips": []map[string]interface{}{
			{
				"id":          clipID,
				"description": "Clip for empty push test",
				"crc":         0,
				"group_id":    "",
				"short_cut":   0,
				"formats": []map[string]interface{}{
					{
						"format_type": 13,
						"data":        base64.StdEncoding.EncodeToString([]byte("Content")),
					},
				},
			},
		},
	}

	statusCode, respBody := testutil.AuthPost(t, server, "/api/v1/clips/sync", tokenA, syncBody)
	require.Equal(t, http.StatusOK, statusCode)
	code, _, _ := testutil.ParseResponse(t, respBody)
	require.Equal(t, 0, code)

	// Login device B
	statusCode, loginRespB, _ := testutil.LoginUserWithDeviceName(t, server, "emptysync", "password123", "PullDevice")
	require.Equal(t, http.StatusOK, statusCode)
	tokenB := getTokenFromLoginResp(t, loginRespB)
	deviceB := getDeviceIDFromLoginResp(t, loginRespB)

	// Device B does an empty pull (no push_clips)
	sinceTime := time.Now().Add(-time.Minute).UTC().Format(time.RFC3339)
	emptyPullBody := map[string]interface{}{
		"since":      sinceTime,
		"device_id":  deviceB,
		"push_clips": []map[string]interface{}{},
	}

	statusCode, respBody = testutil.AuthPost(t, server, "/api/v1/clips/sync", tokenB, emptyPullBody)
	require.Equal(t, http.StatusOK, statusCode)
	code, _, data := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)

	// Should still receive the clip from device A
	newClips, ok := data["new_clips"].([]interface{})
	require.True(t, ok)
	require.Len(t, newClips, 1, "empty push should still return changes from other devices")

	receivedClip := newClips[0].(map[string]interface{})
	assert.Equal(t, clipID, receivedClip["id"])
}
