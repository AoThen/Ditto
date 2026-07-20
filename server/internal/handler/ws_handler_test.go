package handler

import (
	"net/http"
	"net/http/httptest"
	"strings"
	"sync/atomic"
	"testing"
	"time"

	"ditto-cloud-server/internal/hub"

	"github.com/gin-gonic/gin"
	"github.com/gorilla/websocket"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// mockWSHub implements WSHub for testing.
type mockWSHub struct {
	registerFn func(userID int64, conn *websocket.Conn) hub.ClientInterface
}

type mockClient struct {
	sendFn      func(msg map[string]interface{})
	writePumpFn func()
	readPumpFn  func()
}

func (m *mockClient) Send(msg map[string]interface{}) {
	if m.sendFn != nil {
		m.sendFn(msg)
	}
}
func (m *mockClient) WritePump() {
	if m.writePumpFn != nil {
		m.writePumpFn()
	}
}
func (m *mockClient) ReadPump() {
	if m.readPumpFn != nil {
		m.readPumpFn()
	}
}

func (m *mockWSHub) Register(userID int64, conn *websocket.Conn) hub.ClientInterface {
	if m.registerFn != nil {
		return m.registerFn(userID, conn)
	}
	return &mockClient{}
}

func setupWSTest(t *testing.T) (*gin.Context, *httptest.ResponseRecorder) {
	w := httptest.NewRecorder()
	c, _ := gin.CreateTestContext(w)
	return c, w
}

func TestHandleWebSocket_MissingUserID(t *testing.T) {
	c, w := setupWSTest(t)
	mock := &mockWSHub{}
	h := &WSHandler{hub: mock, cfg: nil}

	h.HandleWebSocket(c)

	assert.Equal(t, http.StatusUnauthorized, w.Code)
}

func TestHandleWebSocket_MissingDeviceID(t *testing.T) {
	c, w := setupWSTest(t)
	c.Set("user_id", uint(1))
	mock := &mockWSHub{}
	h := &WSHandler{hub: mock, cfg: nil}

	h.HandleWebSocket(c)

	assert.Equal(t, http.StatusUnauthorized, w.Code)
}

func TestHandleWebSocket_Success(t *testing.T) {
	var connected atomic.Bool
	mock := &mockWSHub{
		registerFn: func(userID int64, conn *websocket.Conn) hub.ClientInterface {
			return &mockClient{
				sendFn: func(msg map[string]interface{}) {
					connected.Store(true)
					assert.Equal(t, "connected", msg["type"])
				},
			}
		},
	}
	h := &WSHandler{hub: mock, cfg: nil}

	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		c, _ := gin.CreateTestContext(w)
		c.Request = r
		c.Set("user_id", uint(1))
		c.Set("device_id", "test-device")
		h.HandleWebSocket(c)
	}))
	defer srv.Close()

	wsURL := "ws" + strings.TrimPrefix(srv.URL, "http")
	conn, _, err := websocket.DefaultDialer.Dial(wsURL, nil)
	require.NoError(t, err)
	conn.Close()

require.Eventually(t, func() bool { return connected.Load() }, time.Second, 10*time.Millisecond)
}

func TestHandleWebSocket_RegisterCalled(t *testing.T) {
	var registered atomic.Bool
	mock := &mockWSHub{
		registerFn: func(userID int64, conn *websocket.Conn) hub.ClientInterface {
			registered.Store(true)
			return &mockClient{
				sendFn: func(msg map[string]interface{}) {
					assert.Equal(t, "connected", msg["type"])
				},
			}
		},
	}
	h := &WSHandler{hub: mock, cfg: nil}

	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		c, _ := gin.CreateTestContext(w)
		c.Request = r
		c.Set("user_id", uint(1))
		c.Set("device_id", "test-device")
		h.HandleWebSocket(c)
	}))
	defer srv.Close()

	wsURL := "ws" + strings.TrimPrefix(srv.URL, "http")
	conn, _, err := websocket.DefaultDialer.Dial(wsURL, nil)
	require.NoError(t, err)
	conn.Close()

	require.Eventually(t, func() bool { return registered.Load() }, time.Second, 10*time.Millisecond)
}

func TestSetAllowedOrigins_EmptyOrigin(t *testing.T) {
	SetAllowedOrigins([]string{"https://example.com"})
	allowed := upgrader.CheckOrigin(&http.Request{Header: http.Header{}})
	assert.True(t, allowed)
}

func TestSetAllowedOrigins_UnknownOrigin(t *testing.T) {
	SetAllowedOrigins([]string{"https://example.com"})
	r := &http.Request{Header: http.Header{"Origin": []string{"https://evil.com"}}}
	allowed := upgrader.CheckOrigin(r)
	assert.False(t, allowed)
}