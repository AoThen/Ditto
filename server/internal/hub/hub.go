package hub

import (
	"log"
	"sync"
	"time"

	"github.com/gorilla/websocket"
)

const (
	// Time allowed to write a message to the peer.
	writeWait = 10 * time.Second

	// Time allowed to read the next pong message from the peer.
	pongWait = 60 * time.Second

	// Send pings to peer with this period. Must be less than pongWait.
	pingPeriod = (pongWait * 9) / 10

	// Maximum message size allowed from peer.
	maxMessageSize = 512
)

// Hub manages all WebSocket connections, grouped by user ID.
type Hub struct {
	// Registered clients per user: userID -> map of *Client
	clients map[int64]map[*Client]bool

	// Mutex to protect the clients map
	mu sync.RWMutex

	// Register channel for new clients
	register chan *Client

	// Unregister channel for disconnected clients
	unregister chan *Client

	// done channel to signal shutdown
	done chan struct{}

	// wg tracks all running goroutines for graceful shutdown
	wg sync.WaitGroup
}

// New creates a new Hub instance.
func New() *Hub {
	return &Hub{
		clients:    make(map[int64]map[*Client]bool),
		register:   make(chan *Client),
		unregister: make(chan *Client),
		done:       make(chan struct{}),
	}
}

// Run starts the hub's main event loop. This should be called as a goroutine.
func (h *Hub) Run() {
	h.wg.Add(1)
	go h.runLoop()
}

func (h *Hub) runLoop() {
	defer h.wg.Done()

	ticker := time.NewTicker(pingPeriod)
	defer ticker.Stop()

	for {
		select {
		case <-h.done:
			h.closeAll()
			return
		case client := <-h.register:
			h.mu.Lock()
			if _, ok := h.clients[client.userID]; !ok {
				h.clients[client.userID] = make(map[*Client]bool)
			}
			h.clients[client.userID][client] = true
			h.mu.Unlock()
			log.Printf("[ws] client registered for user_id=%d, total connections: %d", client.userID, len(h.clients[client.userID]))

		case client := <-h.unregister:
			h.mu.Lock()
			if conns, ok := h.clients[client.userID]; ok {
				if _, ok := conns[client]; ok {
					delete(conns, client)
					close(client.send)
					if len(conns) == 0 {
						delete(h.clients, client.userID)
					}
					log.Printf("[ws] client unregistered for user_id=%d, remaining connections: %d", client.userID, len(h.clients[client.userID]))
				}
			}
			h.mu.Unlock()

		case <-ticker.C:
			h.broadcastPingToAll()
		}
	}
}

// Register adds a new WebSocket connection for a user.
func (h *Hub) Register(userID int64, conn *websocket.Conn) *Client {
	client := &Client{
		hub:      h,
		conn:     conn,
		send:     make(chan map[string]interface{}, 256),
		userID:   userID,
		done:     make(chan struct{}),
	}

	h.register <- client
	return client
}

// Unregister removes a WebSocket connection.
func (h *Hub) Unregister(client *Client) {
	h.unregister <- client
}

// Broadcast sends a message to ALL connections of a user.
func (h *Hub) Broadcast(userID int64, msgType string, data map[string]interface{}) {
	msg := map[string]interface{}{
		"type": msgType,
		"data": data,
	}

	h.mu.RLock()
	conns, ok := h.clients[userID]
	h.mu.RUnlock()

	if !ok {
		return
	}

	for client := range conns {
		select {
		case client.send <- msg:
		default:
			// If send channel is full, skip this client (likely slow/unresponsive)
			log.Printf("[ws] send channel full for user_id=%d, skipping", userID)
		}
	}
}

// BroadcastToOthers sends a message to all connections of a user EXCEPT the sender.
// excludeConn can be *websocket.Conn or nil (nil means broadcast to all).
func (h *Hub) BroadcastToOthers(userID int64, excludeConn interface{}, msgType string, data map[string]interface{}) {
	msg := map[string]interface{}{
		"type": msgType,
		"data": data,
	}

	h.mu.RLock()
	conns, ok := h.clients[userID]
	h.mu.RUnlock()

	if !ok {
		return
	}

	for client := range conns {
		if excludeConn != nil && client.conn == excludeConn {
			continue
		}
		select {
		case client.send <- msg:
		default:
			log.Printf("[ws] send channel full for user_id=%d (broadcast to others), skipping", userID)
		}
	}
}

// broadcastPingToAll sends a ping heartbeat to all connected clients.
func (h *Hub) broadcastPingToAll() {
	msg := map[string]interface{}{
		"type": "ping",
		"data": map[string]interface{}{},
	}

	h.mu.RLock()
	defer h.mu.RUnlock()

	for userID, conns := range h.clients {
		for client := range conns {
			select {
			case client.send <- msg:
			default:
				log.Printf("[ws] ping send failed for user_id=%d", userID)
			}
		}
	}
}

// closeAll closes all connections and stops the hub.
func (h *Hub) closeAll() {
	h.mu.Lock()
	defer h.mu.Unlock()

	goawayMsg := map[string]interface{}{
		"type": "goaway",
		"data": map[string]interface{}{
			"message": "服务器正在关闭连接",
		},
	}

	for userID, conns := range h.clients {
		for client := range conns {
			// Try to send goaway message with a short timeout
			select {
			case client.send <- goawayMsg:
			default:
			}
			close(client.send)
			log.Printf("[ws] closing connection for user_id=%d", userID)
		}
		delete(h.clients, userID)
	}
}

// Shutdown gracefully shuts down the hub.
func (h *Hub) Shutdown() {
	close(h.done)
	h.wg.Wait()
	log.Println("[ws] hub shut down")
}

// UserConnectionCount returns the number of connections for a user (for testing/debugging).
func (h *Hub) UserConnectionCount(userID int64) int {
	h.mu.RLock()
	defer h.mu.RUnlock()
	if conns, ok := h.clients[userID]; ok {
		return len(conns)
	}
	return 0
}
