package hub

import (
	"encoding/json"
	"errors"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/gorilla/websocket"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// --- mocks ---

type publishEntry struct {
	userID int64
	data   []byte
}

type recvEntry struct {
	userID int64
	data   []byte
	err    error
}

type mockBackend struct {
	mu        sync.Mutex
	published []publishEntry
	recvCh    chan recvEntry
	done      chan struct{}
}

func newMockBackend() *mockBackend {
	return &mockBackend{
		recvCh: make(chan recvEntry, 10),
		done:   make(chan struct{}),
	}
}

func (m *mockBackend) Publish(userID int64, data []byte) error {
	m.mu.Lock()
	m.published = append(m.published, publishEntry{userID, data})
	m.mu.Unlock()
	return nil
}

func (m *mockBackend) Subscribe(_ int64) error { return nil }
func (m *mockBackend) Unsubscribe(_ int64) error { return nil }

func (m *mockBackend) Receive() (int64, []byte, error) {
	select {
	case r := <-m.recvCh:
		return r.userID, r.data, r.err
	case <-m.done:
		return 0, nil, errors.New("closed")
	}
}

func (m *mockBackend) Close() error {
	select {
	case <-m.done:
	default:
		close(m.done)
	}
	return nil
}

// --- helpers ---

func dialTestWS(t *testing.T) (*websocket.Conn, func()) {
	t.Helper()
	s := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		upgrader := websocket.Upgrader{}
		conn, err := upgrader.Upgrade(w, r, nil)
		require.NoError(t, err)
		go func() {
			for {
				_, _, err := conn.ReadMessage()
				if err != nil {
					return
				}
			}
		}()
	}))
	url := "ws" + strings.TrimPrefix(s.URL, "http")
	conn, _, err := websocket.DefaultDialer.Dial(url, nil)
	require.NoError(t, err)
	return conn, func() {
		conn.Close()
		s.Close()
	}
}

// waitForCount polls until UserConnectionCount reaches the expected value.
// Register sends to an unbuffered channel and returns before runLoop adds
// the client to the map, so callers must synchronize before proceeding.
func waitForCount(t *testing.T, h *Hub, userID int64, expected int) {
	t.Helper()
	require.Eventually(t, func() bool {
		return h.UserConnectionCount(userID) == expected
	}, time.Second, time.Millisecond)
}

// drainAndVerifyClosed drains all buffered messages from ch and asserts the
// channel is closed.
func drainAndVerifyClosed(t *testing.T, ch <-chan map[string]interface{}) {
	t.Helper()
	deadline := time.After(time.Second)
	for {
		select {
		case _, ok := <-ch:
			if !ok {
				return
			}
		case <-deadline:
			t.Fatal("channel not closed within timeout")
			return
		}
	}
}

// --- tests ---

func TestNewHub_DefaultBackend(t *testing.T) {
	h := New()
	assert.NotNil(t, h)
	assert.NotNil(t, h.clients)
	assert.NotNil(t, h.register)
	assert.NotNil(t, h.unregister)
	assert.NotNil(t, h.done)
	assert.NotNil(t, h.backend)
	_, ok := h.backend.(*localBackend)
	assert.True(t, ok, "default backend should be localBackend")
}

func TestRegister_AddsClientToMap(t *testing.T) {
	h := New()
	h.Run()
	defer h.Shutdown()

	client := h.Register(1, nil).(*Client)
	assert.NotNil(t, client)
	assert.Equal(t, int64(1), client.userID)
	waitForCount(t, h, 1, 1)
}

func TestRegister_MultipleClientsForSameUser(t *testing.T) {
	h := New()
	h.Run()
	defer h.Shutdown()

	for i := 0; i < 3; i++ {
		h.Register(1, nil)
	}
	waitForCount(t, h, 1, 3)
}

func TestUnregister_RemovesClient(t *testing.T) {
	h := New()
	h.Run()
	defer h.Shutdown()

	client := h.Register(1, nil).(*Client)
	waitForCount(t, h, 1, 1)

	h.Unregister(client)
	require.Eventually(t, func() bool {
		return h.UserConnectionCount(1) == 0
	}, time.Second, 5*time.Millisecond)
}

func TestUnregister_OnlyRemovesSpecificClient(t *testing.T) {
	h := New()
	h.Run()
	defer h.Shutdown()

	c1 := h.Register(1, nil).(*Client)
	h.Register(1, nil)
	waitForCount(t, h, 1, 2)

	h.Unregister(c1)
	require.Eventually(t, func() bool {
		return h.UserConnectionCount(1) == 1
	}, time.Second, 5*time.Millisecond)
}

func TestBroadcast_DeliversToAllConnections(t *testing.T) {
	h := New()
	h.Run()
	defer h.Shutdown()

	c1 := h.Register(1, nil).(*Client)
	c2 := h.Register(1, nil).(*Client)
	waitForCount(t, h, 1, 2)

	testData := map[string]interface{}{"key": "value"}
	h.Broadcast(1, "test_type", testData)

	select {
	case msg := <-c1.send:
		assert.Equal(t, "test_type", msg["type"])
		assert.Equal(t, testData, msg["data"])
	case <-time.After(time.Second):
		t.Fatal("client 1 did not receive message")
	}

	select {
	case msg := <-c2.send:
		assert.Equal(t, "test_type", msg["type"])
		assert.Equal(t, testData, msg["data"])
	case <-time.After(time.Second):
		t.Fatal("client 2 did not receive message")
	}
}

func TestBroadcastToOthers_ExcludesSender(t *testing.T) {
	senderConn, cleanup := dialTestWS(t)
	defer cleanup()

	h := New()
	h.Run()
	defer h.Shutdown()

	sender := h.Register(1, senderConn).(*Client)
	other := h.Register(1, nil).(*Client)
	waitForCount(t, h, 1, 2)

	testData := map[string]interface{}{"key": "value"}
	h.BroadcastToOthers(1, senderConn, "test_type", testData)

	select {
	case <-sender.send:
		t.Fatal("sender should not receive message")
	case <-time.After(100 * time.Millisecond):
	}

	select {
	case msg := <-other.send:
		assert.Equal(t, "test_type", msg["type"])
		assert.Equal(t, testData, msg["data"])
	case <-time.After(time.Second):
		t.Fatal("other client did not receive message")
	}
}

func TestBroadcastToOthers_PublishesToBackend(t *testing.T) {
	b := newMockBackend()
	h := New(b)
	h.Run()

	h.Register(1, nil)
	waitForCount(t, h, 1, 1)

	testData := map[string]interface{}{"key": "value"}
	h.BroadcastToOthers(1, nil, "test_type", testData)

	require.Eventually(t, func() bool {
		b.mu.Lock()
		defer b.mu.Unlock()
		return len(b.published) == 1
	}, time.Second, 5*time.Millisecond)

	h.Shutdown()

	b.mu.Lock()
	defer b.mu.Unlock()
	require.Len(t, b.published, 1)

	var msg map[string]interface{}
	require.NoError(t, json.Unmarshal(b.published[0].data, &msg))
	assert.Equal(t, "test_type", msg["type"])
	assert.Equal(t, testData, msg["data"])
	assert.Equal(t, int64(1), b.published[0].userID)
}

func TestShutdown_ClosesAllConnections(t *testing.T) {
	h := New()
	h.Run()

	c1 := h.Register(1, nil).(*Client)
	c2 := h.Register(2, nil).(*Client)
	waitForCount(t, h, 1, 1)
	waitForCount(t, h, 2, 1)

	h.Shutdown()

	drainAndVerifyClosed(t, c1.send)
	drainAndVerifyClosed(t, c2.send)
}

func TestUserConnectionCount_NonExistentUser(t *testing.T) {
	h := New()
	assert.Equal(t, 0, h.UserConnectionCount(999))
}

func TestBroadcast_NonBlockingOnFullChannel(t *testing.T) {
	h := New()
	h.Run()
	defer h.Shutdown()

	client := h.Register(1, nil).(*Client)
	waitForCount(t, h, 1, 1)

	for i := 0; i < 256; i++ {
		client.send <- map[string]interface{}{"type": "fill"}
	}

	done := make(chan struct{})
	go func() {
		h.Broadcast(1, "overflow", nil)
		close(done)
	}()

	select {
	case <-done:
	case <-time.After(time.Second):
		t.Fatal("Broadcast blocked on full channel")
	}
}

func TestBackendIntegration_ForwardToClients(t *testing.T) {
	b := newMockBackend()
	h := New(b)
	h.Run()
	defer h.Shutdown()

	client := h.Register(1, nil).(*Client)
	waitForCount(t, h, 1, 1)

	msgData := map[string]interface{}{"type": "backend_msg", "data": "hello"}
	raw, err := json.Marshal(msgData)
	require.NoError(t, err)

	b.recvCh <- recvEntry{userID: 1, data: raw}

	select {
	case msg := <-client.send:
		assert.Equal(t, "backend_msg", msg["type"])
		assert.Equal(t, "hello", msg["data"])
	case <-time.After(time.Second):
		t.Fatal("client did not receive backend message")
	}
}
