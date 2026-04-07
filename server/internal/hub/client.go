package hub

import (
	"encoding/json"
	"log"
	"time"

	"github.com/gorilla/websocket"
)

// Client represents a single WebSocket connection.
type Client struct {
	hub    *Hub
	conn   *websocket.Conn
	send   chan map[string]interface{}
	userID int64
	done   chan struct{}
}

// ReadPump reads messages from the WebSocket connection and pumps them to the hub.
// This runs in its own goroutine and handles client-initiated messages.
func (c *Client) ReadPump() {
	defer func() {
		if r := recover(); r != nil {
			log.Printf("[ws] ReadPump recovered from panic for user_id=%d: %v", c.userID, r)
		}
		c.hub.Unregister(c)
		c.conn.Close()
		log.Printf("[ws] ReadPump closed for user_id=%d", c.userID)
	}()

	c.conn.SetReadLimit(maxMessageSize)
	c.conn.SetReadDeadline(time.Now().Add(pongWait))
	c.conn.SetPongHandler(func(string) error {
		c.conn.SetReadDeadline(time.Now().Add(pongWait))
		return nil
	})

	for {
		_, message, err := c.conn.ReadMessage()
		if err != nil {
			if websocket.IsUnexpectedCloseError(err, websocket.CloseGoingAway, websocket.CloseNormalClosure) {
				log.Printf("[ws] ReadPump error for user_id=%d: %v", c.userID, err)
			} else {
				log.Printf("[ws] ReadPump closed for user_id=%d: %v", c.userID, err)
			}
			return
		}

		// Handle client messages (control messages only, max 512 bytes)
		var msg map[string]interface{}
		if err := json.Unmarshal(message, &msg); err != nil {
			log.Printf("[ws] invalid JSON from user_id=%d: %v", c.userID, err)
			continue
		}

		// Handle pong response from client
		if msgType, ok := msg["type"].(string); ok && msgType == "pong" {
			// Client responded to our ping, connection is alive
			continue
		}

		// Log any other messages (clients shouldn't send data messages)
		log.Printf("[ws] received message from user_id=%d: %s", c.userID, string(message))
	}
}

// WritePump writes messages from the send channel to the WebSocket connection.
// This runs in its own goroutine and handles server-initiated messages.
func (c *Client) WritePump() {
	ticker := time.NewTicker(pingPeriod)
	defer func() {
		ticker.Stop()
		if r := recover(); r != nil {
			log.Printf("[ws] WritePump recovered from panic for user_id=%d: %v", c.userID, r)
		}
		c.conn.Close()
		log.Printf("[ws] WritePump closed for user_id=%d", c.userID)
	}()

	for {
		select {
		case <-c.done:
			return
		case message, ok := <-c.send:
			c.conn.SetWriteDeadline(time.Now().Add(writeWait))
			if !ok {
				// Hub closed the channel
				c.conn.WriteMessage(websocket.CloseMessage, []byte{})
				return
			}

			data, err := json.Marshal(message)
			if err != nil {
				log.Printf("[ws] failed to marshal message for user_id=%d: %v", c.userID, err)
				continue
			}

			w, err := c.conn.NextWriter(websocket.TextMessage)
			if err != nil {
				return
			}
			w.Write(data)

			// Send any queued messages in the same websocket frame batch
			n := len(c.send)
			for i := 0; i < n; i++ {
				w.Write([]byte{'\n'})
				msg, ok := <-c.send
				if !ok {
					break
				}
				data, _ := json.Marshal(msg)
				w.Write(data)
			}

			if err := w.Close(); err != nil {
				return
			}

		case <-ticker.C:
			c.conn.SetWriteDeadline(time.Now().Add(writeWait))
			if err := c.conn.WriteMessage(websocket.PingMessage, nil); err != nil {
				return
			}
		}
	}
}

// Send queues a message for sending to this client.
func (c *Client) Send(msg map[string]interface{}) {
	select {
	case c.send <- msg:
	default:
		log.Printf("[ws] send channel full for user_id=%d, message dropped", c.userID)
	}
}

// Close signals the client to shut down.
func (c *Client) Close() {
	select {
	case <-c.done:
	default:
		close(c.done)
	}
}
