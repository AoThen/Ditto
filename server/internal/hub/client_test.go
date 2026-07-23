package hub

import (
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
)

func TestSend_NonBlockingDrop(t *testing.T) {
	client := &Client{
		send: make(chan map[string]interface{}, 1),
	}

	client.send <- map[string]interface{}{"type": "msg1"}

	client.Send(map[string]interface{}{"type": "msg2"})

	assert.Equal(t, 1, len(client.send))
}

func TestClose_Idempotent(t *testing.T) {
	client := &Client{
		done: make(chan struct{}),
	}

	client.Close()
	client.Close()
	client.Close()

	_, ok := <-client.done
	assert.False(t, ok, "done channel should be closed")
}

func TestSend_AddsToChannel(t *testing.T) {
	client := &Client{
		send: make(chan map[string]interface{}, 256),
	}
	msg := map[string]interface{}{"type": "test", "data": "value"}

	client.Send(msg)

	select {
	case received := <-client.send:
		assert.Equal(t, msg, received)
	case <-time.After(time.Second):
		t.Fatal("message not received from send channel")
	}
}

func TestClient_StructInit(t *testing.T) {
	h := New()
	client := &Client{
		hub:    h,
		send:   make(chan map[string]interface{}, 256),
		userID: 42,
		done:   make(chan struct{}),
	}

	assert.NotNil(t, client.hub)
	assert.NotNil(t, client.send)
	assert.NotNil(t, client.done)
	assert.Equal(t, int64(42), client.userID)
	assert.Equal(t, 256, cap(client.send))
}
