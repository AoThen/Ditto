package tests

import (
	"bytes"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"net/http"
	"strings"
	"testing"
	"time"

	"ditto-cloud-server/tests/testutil"

	"github.com/gorilla/websocket"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// wsDialURL converts an httptest.Server URL to a WebSocket URL.
// e.g. "http://127.0.0.1:12345" -> "ws://127.0.0.1:12345/api/v1/ws"
func wsDialURL(serverURL, token string) string {
	path := strings.TrimPrefix(serverURL, "http://")
	return "ws://" + path + "/api/v1/ws"
}

// wsHeaders returns HTTP headers for WebSocket connection with auth.
// The Authorization header is for the Auth middleware, while the
// Sec-WebSocket-Protocol subprotocol is for the WS handler (CRITICAL FIX C3).
func wsHeaders(token string) http.Header {
	h := http.Header{}
	h.Set("Authorization", "Bearer "+token)
	return h
}

// readJSONMessage reads a single JSON message from the WebSocket connection.
func readJSONMessage(t *testing.T, conn *websocket.Conn) map[string]interface{} {
	t.Helper()
	_, msg, err := conn.ReadMessage()
	require.NoError(t, err, "failed to read WebSocket message")

	var parsed map[string]interface{}
	err = json.Unmarshal(msg, &parsed)
	require.NoError(t, err, "failed to parse WebSocket message as JSON: %s", string(msg))
	return parsed
}

// pushClipFromDevice pushes a clip via the sync API from the given device.
func pushClipFromDevice(t *testing.T, serverURL, token, deviceID, clipID, description string) {
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
						"data":        base64.StdEncoding.EncodeToString([]byte("clip data")),
					},
				},
			},
		},
	}

	reqBody, _ := json.Marshal(syncBody)
	req, err := http.NewRequest("POST", serverURL+"/api/v1/clips/sync", bytes.NewReader(reqBody))
	require.NoError(t, err)
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("Authorization", "Bearer "+token)

	resp, err := http.DefaultClient.Do(req)
	require.NoError(t, err)
	defer resp.Body.Close()

	require.Equal(t, http.StatusOK, resp.StatusCode, "clip push failed with status %d", resp.StatusCode)
}

// ============================================================================
// Test 1: TestWebSocket_Connect
// Verify that a valid token allows WebSocket connection and returns "connected" message.
// ============================================================================
func TestWebSocket_Connect(t *testing.T) {
	server, _, _ := testutil.SetupTestServerWithWS(t)

	// Register and login to get a device token
	token, _ := testutil.RegisterAndLogin(t, server, "wsuser", "wsuser@example.com", "password123")

	// Connect to WebSocket
	wsURL := wsDialURL(server.URL, token)
	dialer := websocket.Dialer{
		Subprotocols: []string{token}, // CRITICAL FIX (C3): token as subprotocol
	}
	conn, resp, err := dialer.Dial(wsURL, wsHeaders(token))
	require.NoError(t, err, "WebSocket connection failed")
	require.Equal(t, http.StatusSwitchingProtocols, resp.StatusCode, "expected HTTP 101 Switching Protocols")
	defer conn.Close()

	// Set read deadline to avoid hanging
	conn.SetReadDeadline(time.Now().Add(5 * time.Second))

	// First message should be "connected"
	msg := readJSONMessage(t, conn)
	assert.Equal(t, "connected", msg["type"], "first message should be 'connected'")

	data, ok := msg["data"].(map[string]interface{})
	require.True(t, ok, "connected message should have data field")
	assert.Equal(t, "已连接到 Ditto Cloud", data["message"])

	// Close cleanly
	err = conn.WriteMessage(websocket.CloseMessage, websocket.FormatCloseMessage(websocket.CloseNormalClosure, ""))
	assert.NoError(t, err)
}

// ============================================================================
// Test 2: TestWebSocket_BroadcastOnSync
// DeviceB connected via WS should receive clip_added when DeviceA pushes via sync.
// ============================================================================
func TestWebSocket_BroadcastOnSync(t *testing.T) {
	server, _, _ := testutil.SetupTestServerWithWS(t)

	// Register user
	testutil.RegisterUser(t, server, "broadcastuser", "broadcastuser@example.com", "password123")

	// Login DeviceA
	statusCode, loginRespA, _ := testutil.LoginUserWithDeviceName(t, server, "broadcastuser", "password123", "DeviceA")
	require.Equal(t, http.StatusOK, statusCode)
	tokenA := getTokenFromLoginResp(t, loginRespA)
	deviceA := getDeviceIDFromLoginResp(t, loginRespA)

	// Login DeviceB
	statusCode, loginRespB, _ := testutil.LoginUserWithDeviceName(t, server, "broadcastuser", "password123", "DeviceB")
	require.Equal(t, http.StatusOK, statusCode)
	tokenB := getTokenFromLoginResp(t, loginRespB)

	// Connect DeviceB to WebSocket
	wsURL := wsDialURL(server.URL, tokenB)
	dialer := websocket.Dialer{
		Subprotocols: []string{tokenB},
	}
	connB, resp, err := dialer.Dial(wsURL, wsHeaders(tokenB))
	require.NoError(t, err, "DeviceB WebSocket connection failed")
	require.Equal(t, http.StatusSwitchingProtocols, resp.StatusCode)
	defer connB.Close()

	connB.SetReadDeadline(time.Now().Add(5 * time.Second))

	// Consume the initial "connected" message
	msg := readJSONMessage(t, connB)
	assert.Equal(t, "connected", msg["type"])

	// DeviceA pushes a clip via sync
	clipID := fmt.Sprintf("clip-ws-broadcast-%d", time.Now().UnixNano())
	pushClipFromDevice(t, server.URL, tokenA, deviceA, clipID, "Broadcast clip from DeviceA")

	// DeviceB should receive the clips_added broadcast
	connB.SetReadDeadline(time.Now().Add(5 * time.Second))
	broadcastMsg := readJSONMessage(t, connB)
	assert.Equal(t, "clips_added", broadcastMsg["type"], "DeviceB should receive clips_added broadcast")

	bData, ok := broadcastMsg["data"].(map[string]interface{})
	require.True(t, ok, "clips_added message should have data field")
	clips, ok := bData["clips"].([]interface{})
	require.True(t, ok, "clips_added message should have clips array")
	require.Len(t, clips, 1, "clips array should have 1 clip")
	clipMap, ok := clips[0].(map[string]interface{})
	require.True(t, ok)
	assert.Equal(t, clipID, clipMap["clip_id"], "broadcast should include the pushed clip's ID")
}

// ============================================================================
// Test 3: TestWebSocket_NoCrossUserBroadcast
// DeviceB1 (user B) should NOT receive broadcasts when user A pushes a clip.
// ============================================================================
func TestWebSocket_NoCrossUserBroadcast(t *testing.T) {
	server, _, _ := testutil.SetupTestServerWithWS(t)

	// Register and login user A (first user, becomes admin)
	testutil.RegisterUser(t, server, "crossuserA", "crossuserA@example.com", "password123")
	statusCode, loginRespA, setCookiesA := testutil.LoginUserWithDeviceName(t, server, "crossuserA", "password123", "DeviceA1")
	require.Equal(t, http.StatusOK, statusCode)
	tokenA := getTokenFromLoginResp(t, loginRespA)
	deviceA1 := getDeviceIDFromLoginResp(t, loginRespA)

	// If token is empty from body, extract from cookie
	if tokenA == "" {
		tokenA = testutil.ExtractCookie(setCookiesA, "device_token")
	}

	// Create user B via admin API using user A's admin token
	statusCode, _ = testutil.AuthPost(t, server, "/api/v1/admin/users", tokenA, map[string]string{
		"username": "crossuserB",
		"email":    "crossuserB@example.com",
		"password": "password123",
	})
	require.Equal(t, http.StatusOK, statusCode)

	// Login user B
	statusCode, loginRespB, _ := testutil.LoginUserWithDeviceName(t, server, "crossuserB", "password123", "DeviceB1")
	require.Equal(t, http.StatusOK, statusCode)
	tokenB := getTokenFromLoginResp(t, loginRespB)

	// Connect DeviceB1 to WebSocket
	wsURL := wsDialURL(server.URL, tokenB)
	dialer := websocket.Dialer{
		Subprotocols: []string{tokenB},
	}
	connB1, resp, err := dialer.Dial(wsURL, wsHeaders(tokenB))
	require.NoError(t, err, "DeviceB1 WebSocket connection failed")
	require.Equal(t, http.StatusSwitchingProtocols, resp.StatusCode)
	defer connB1.Close()

	connB1.SetReadDeadline(time.Now().Add(5 * time.Second))

	// Consume the initial "connected" message
	msg := readJSONMessage(t, connB1)
	assert.Equal(t, "connected", msg["type"])

	// User A pushes a clip
	clipID := fmt.Sprintf("clip-crossuser-%d", time.Now().UnixNano())
	pushClipFromDevice(t, server.URL, tokenA, deviceA1, clipID, "Clip from user A")

	// DeviceB1 should NOT receive any message (set a short read deadline)
	connB1.SetReadDeadline(time.Now().Add(1 * time.Second))
	_, _, err = connB1.ReadMessage()

	// We expect a timeout error (net.Error with Timeout() == true)
	require.Error(t, err, "DeviceB1 should NOT receive any message from user A's push")
	assert.True(t, isTimeoutError(err), "expected timeout error, got: %v", err)
}

// ============================================================================
// Test 4: TestWebSocket_InvalidToken
// Connection with an invalid JWT should be rejected.
// ============================================================================
func TestWebSocket_InvalidToken(t *testing.T) {
	server, _, _ := testutil.SetupTestServerWithWS(t)

	// Try to connect with an invalid JWT token
	invalidToken := "this.is.not.a.valid.jwt.token"
	wsURL := wsDialURL(server.URL, invalidToken)
	dialer := websocket.Dialer{
		Subprotocols: []string{invalidToken},
	}

	conn, resp, err := dialer.Dial(wsURL, wsHeaders(invalidToken))

	// Connection should fail (HTTP upgrade rejected)
	if err == nil && conn != nil {
		// If connection somehow succeeded, close it and fail the test
		conn.Close()
		t.Fatal("expected WebSocket connection to be rejected with invalid token")
	}

	// Response should indicate rejection (not 101)
	require.NotNil(t, resp, "expected HTTP response")
	assert.NotEqual(t, http.StatusSwitchingProtocols, resp.StatusCode,
		"expected non-101 response for invalid token, got %d", resp.StatusCode)
	assert.True(t, resp.StatusCode == http.StatusUnauthorized || resp.StatusCode == http.StatusBadRequest,
		"expected 401 or 400 for invalid token, got %d", resp.StatusCode)
}

// ============================================================================
// Test 5: TestWebSocket_MultipleConnections
// All 3 connected clients for the same user should receive the broadcast.
// ============================================================================
func TestWebSocket_MultipleConnections(t *testing.T) {
	server, _, _ := testutil.SetupTestServerWithWS(t)

	// Register user
	testutil.RegisterUser(t, server, "multiuser", "multiuser@example.com", "password123")

	// Login 3 times for 3 device tokens
	statusCode, loginResp1, _ := testutil.LoginUserWithDeviceName(t, server, "multiuser", "password123", "Client1")
	require.Equal(t, http.StatusOK, statusCode)
	token1 := getTokenFromLoginResp(t, loginResp1)
	device1 := getDeviceIDFromLoginResp(t, loginResp1)

	statusCode, loginResp2, _ := testutil.LoginUserWithDeviceName(t, server, "multiuser", "password123", "Client2")
	require.Equal(t, http.StatusOK, statusCode)
	token2 := getTokenFromLoginResp(t, loginResp2)

	statusCode, loginResp3, _ := testutil.LoginUserWithDeviceName(t, server, "multiuser", "password123", "Client3")
	require.Equal(t, http.StatusOK, statusCode)
	token3 := getTokenFromLoginResp(t, loginResp3)

	// Connect all 3 WebSocket clients
	connections := make([]*websocket.Conn, 3)
	tokens := []string{token1, token2, token3}
	for i, tok := range tokens {
		wsURL := wsDialURL(server.URL, tok)
		dialer := websocket.Dialer{
			Subprotocols: []string{tok},
		}
		conn, resp, err := dialer.Dial(wsURL, wsHeaders(tok))
		require.NoError(t, err, "Client %d WebSocket connection failed", i+1)
		require.Equal(t, http.StatusSwitchingProtocols, resp.StatusCode)
		connections[i] = conn
		defer conn.Close()
		conn.SetReadDeadline(time.Now().Add(5 * time.Second))

		// Consume initial "connected" message
		msg := readJSONMessage(t, conn)
		assert.Equal(t, "connected", msg["type"], "Client %d should receive connected", i+1)
	}

	// Client1 pushes a clip via sync
	clipID := fmt.Sprintf("clip-multi-%d", time.Now().UnixNano())
	pushClipFromDevice(t, server.URL, token1, device1, clipID, "Multi-client broadcast clip")

	// All 3 clients should receive the clips_added broadcast
	for i, conn := range connections {
		conn.SetReadDeadline(time.Now().Add(5 * time.Second))
		broadcastMsg := readJSONMessage(t, conn)
		assert.Equal(t, "clips_added", broadcastMsg["type"], "Client %d should receive clips_added", i+1)

		bData, ok := broadcastMsg["data"].(map[string]interface{})
		require.True(t, ok, "Client %d: clips_added should have data field", i+1)
		clips, ok := bData["clips"].([]interface{})
		require.True(t, ok, "Client %d: clips_added should have clips array", i+1)
		require.Len(t, clips, 1, "Client %d: clips array should have 1 clip", i+1)
		clipMap, ok := clips[0].(map[string]interface{})
		require.True(t, ok)
		assert.Equal(t, clipID, clipMap["clip_id"], "Client %d: broadcast should include clip ID", i+1)
	}
}

// isTimeoutError checks if the error is a timeout.
func isTimeoutError(err error) bool {
	if err == nil {
		return false
	}
	type timeout interface {
		Timeout() bool
	}
	te, ok := err.(timeout)
	return ok && te.Timeout()
}
