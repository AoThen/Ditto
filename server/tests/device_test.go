package tests

import (
	"encoding/json"
	"net/http"
	"testing"

	"ditto-cloud-server/tests/testutil"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// DeviceInfo represents a device entry in the device list response.
type DeviceInfo struct {
	ID         string `json:"id"`
	DeviceName string `json:"device_name"`
	LastSeen   string `json:"last_seen"`
}

// parseDeviceListResponse parses the raw JSON response and returns the device list.
// The device list endpoint returns data as a direct array (not wrapped in PaginatedResponse).
func parseDeviceListResponse(t *testing.T, respBody []byte) (code int, devices []DeviceInfo) {
	t.Helper()

	var resp map[string]interface{}
	if err := json.Unmarshal(respBody, &resp); err != nil {
		t.Fatalf("failed to unmarshal response: %v\nbody: %s", err, string(respBody))
	}

	codeFloat, _ := resp["code"].(float64)
	code = int(codeFloat)

	// The data field contains the device list directly as an array
	dataRaw := resp["data"]
	if dataRaw == nil {
		return code, nil
	}

	dataBytes, err := json.Marshal(dataRaw)
	if err != nil {
		return code, nil
	}

	if err := json.Unmarshal(dataBytes, &devices); err != nil {
		t.Fatalf("failed to unmarshal device list: %v", err)
	}

	return code, devices
}

// TestDevice_ListAfterLogin — after login, list devices, expect at least 1
func TestDevice_ListAfterLogin(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	user := testutil.CreateTestUser(t, server)

	// List devices
	statusCode, respBody := testutil.AuthGet(t, server, "/api/v1/devices", user.Token)
	assert.Equal(t, http.StatusOK, statusCode)
	code, devices := parseDeviceListResponse(t, respBody)
	assert.Equal(t, 0, code)
	require.NotEmpty(t, devices, "should have at least one device after login")

	// Verify the current device is in the list
	found := false
	for _, d := range devices {
		if d.ID == user.DeviceID {
			found = true
			break
		}
	}
	assert.True(t, found, "current device should be in the device list")
}

// TestDevice_Remove — remove a device, expect it gone from list
func TestDevice_Remove(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	// Register user
	testutil.RegisterUser(t, server, "devuser", "devuser@example.com", "password123")

	// Login with device A
	statusCode, loginRespA, _ := testutil.LoginUserWithDeviceName(t, server, "devuser", "password123", "DeviceToRemove")
	require.Equal(t, http.StatusOK, statusCode)
	deviceA := getDeviceIDFromLoginResp(t, loginRespA)

	// Login with device B
	statusCode, loginRespB, _ := testutil.LoginUserWithDeviceName(t, server, "devuser", "password123", "DeviceToKeep")
	require.Equal(t, http.StatusOK, statusCode)
	tokenB := getTokenFromLoginResp(t, loginRespB)

	// List devices - should have 2
	statusCode, respBody := testutil.AuthGet(t, server, "/api/v1/devices", tokenB)
	require.Equal(t, http.StatusOK, statusCode)
	code, devices := parseDeviceListResponse(t, respBody)
	assert.Equal(t, 0, code)
	require.Len(t, devices, 2, "should have 2 devices")

	// Remove device A using token B
	statusCode, respBody = testutil.AuthDelete(t, server, "/api/v1/devices/"+deviceA, tokenB)
	assert.Equal(t, http.StatusOK, statusCode)
	code, message, _ := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)
	assert.Equal(t, "设备已移除", message)

	// List devices again - should have only 1
	statusCode, respBody = testutil.AuthGet(t, server, "/api/v1/devices", tokenB)
	require.Equal(t, http.StatusOK, statusCode)
	code, devices = parseDeviceListResponse(t, respBody)
	assert.Equal(t, 0, code)
	require.Len(t, devices, 1, "should have 1 device after removal")

	// Verify the correct device remains
	assert.Equal(t, "dev-1-DeviceToKeep", devices[0].ID)
}
