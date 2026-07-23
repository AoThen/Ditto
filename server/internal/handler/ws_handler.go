package handler

import (
	"log"
	"net/http"

	"ditto-cloud-server/internal/config"
	"ditto-cloud-server/internal/hub"
	"ditto-cloud-server/internal/response"

	"github.com/gin-gonic/gin"
	"github.com/gorilla/websocket"
)

// WSHub interface abstracts the hub for dependency injection.
type WSHub interface {
	Register(userID int64, conn *websocket.Conn) hub.ClientInterface
}

var upgrader = websocket.Upgrader{
	ReadBufferSize:  1024,
	WriteBufferSize: 1024,
	CheckOrigin: func(r *http.Request) bool {
		// HIGH FIX (H5): Validate Origin header against allowed list
		origin := r.Header.Get("Origin")
		if origin == "" {
			return true // Allow non-browser clients
		}
		for _, allowed := range upgraderConfig.allowedOrigins {
			if origin == allowed {
				return true
			}
		}
		log.Printf("[ws] blocked origin: %s", origin)
		return false
	},
}

var upgraderConfig = struct {
	allowedOrigins []string
}{}

// SetAllowedOrigins configures the allowed origins for WebSocket connections.
func SetAllowedOrigins(origins []string) {
	upgraderConfig.allowedOrigins = origins
}

// WSHandler handles WebSocket upgrade and connection lifecycle.
type WSHandler struct {
	hub WSHub
	cfg *config.Config
}

// NewWSHandler creates a new WebSocket handler.
func NewWSHandler(h WSHub, cfg *config.Config) *WSHandler {
	return &WSHandler{hub: h, cfg: cfg}
}

// HandleWebSocket upgrades HTTP to WebSocket and manages the connection.
func (h *WSHandler) HandleWebSocket(c *gin.Context) {
	// Auth middleware has already validated the JWT and set user_id/device_id in context.
	userIDAny, exists := c.Get("user_id")
	if !exists {
		response.Error(c, http.StatusUnauthorized, 40100, "未提供认证令牌")
		return
	}
	userID := userIDAny.(uint)

	deviceIDAny, exists := c.Get("device_id")
	if !exists {
		response.Error(c, http.StatusUnauthorized, 40100, "未提供设备令牌")
		return
	}
	deviceID := deviceIDAny.(string)

	// Upgrade HTTP to WebSocket
	conn, err := upgrader.Upgrade(c.Writer, c.Request, nil)
	if err != nil {
		log.Printf("[ws] failed to upgrade connection: %v", err)
		return
	}

	// Register with hub
	client := h.hub.Register(int64(userID), conn)

	log.Printf("[ws] new connection established for user_id=%d, device_id=%s", userID, deviceID)

	// Send initial "connected" message
	client.Send(map[string]interface{}{
		"type": "connected",
		"data": map[string]interface{}{
			"message": "已连接到 Ditto Cloud",
		},
	})

	// Start read and write pumps
	go client.WritePump()
	go client.ReadPump()
}