package handler

import (
	"log"
	"net/http"

	"ditto-cloud-server/internal/config"
	"ditto-cloud-server/internal/hub"
	"ditto-cloud-server/internal/response"

	"github.com/gin-gonic/gin"
	"github.com/golang-jwt/jwt/v5"
	"github.com/gorilla/websocket"
)

// WSHub interface abstracts the hub for dependency injection.
type WSHub interface {
	Register(userID int64, conn *websocket.Conn) *hub.Client
}

// WSJWTClaims mirrors the claims structure used in auth middleware.
type WSJWTClaims struct {
	UserID   uint   `json:"user_id"`
	DeviceID string `json:"device_id"`
	jwt.RegisteredClaims
}

var upgrader = websocket.Upgrader{
	ReadBufferSize:  1024,
	WriteBufferSize: 1024,
	CheckOrigin: func(r *http.Request) bool {
		// Allow all origins for WebSocket (can be restricted in production)
		return true
	},
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
	// Validate JWT token from query param
	tokenStr := c.Query("token")
	if tokenStr == "" {
		response.Error(c, http.StatusUnauthorized, 40100, "未提供认证令牌")
		return
	}

	claims := &WSJWTClaims{}
	token, err := jwt.ParseWithClaims(tokenStr, claims, func(t *jwt.Token) (interface{}, error) {
		return []byte(h.cfg.JWTSecret), nil
	})
	if err != nil || !token.Valid {
		response.Error(c, http.StatusUnauthorized, 40102, "无效的认证令牌")
		return
	}

	if claims.UserID == 0 {
		response.Error(c, http.StatusUnauthorized, 40103, "令牌中缺少用户信息")
		return
	}

	// Upgrade HTTP to WebSocket
	conn, err := upgrader.Upgrade(c.Writer, c.Request, nil)
	if err != nil {
		log.Printf("[ws] failed to upgrade connection: %v", err)
		return
	}

	// Register with hub
	client := h.hub.Register(int64(claims.UserID), conn)

	log.Printf("[ws] new connection established for user_id=%d, device_id=%s", claims.UserID, claims.DeviceID)

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
