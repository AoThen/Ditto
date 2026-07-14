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
	// H1 + C3: Read JWT from HttpOnly cookie (primary) or Sec-WebSocket-Protocol (fallback)
	tokenStr := ""

	// Try HttpOnly cookie first (set by login, read by auth middleware)
	if cookie, err := c.Cookie("device_token"); err == nil && cookie != "" {
		tokenStr = cookie
	}

	// Fallback: Sec-WebSocket-Protocol header (for clients that don't support cookies)
	if tokenStr == "" {
		tokenStr = c.GetHeader("Sec-WebSocket-Protocol")
	}

	// Last fallback: raw request header
	if tokenStr == "" {
		tokenStr = c.Request.Header.Get("Sec-Websocket-Protocol")
	}

	if tokenStr == "" {
		response.Error(c, http.StatusUnauthorized, 40100, "未提供认证令牌")
		return
	}

	claims := &WSJWTClaims{}
	token, err := jwt.ParseWithClaims(tokenStr, claims, func(t *jwt.Token) (interface{}, error) {
		// MEDIUM FIX (M6): Enforce HMAC algorithm to prevent alg=None / algorithm confusion
		if _, ok := t.Method.(*jwt.SigningMethodHMAC); !ok {
			return nil, jwt.ErrSignatureInvalid
		}
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
	// Must respond with the same protocol to complete handshake
	upgraderWithProtocol := websocket.Upgrader{
		ReadBufferSize:  1024,
		WriteBufferSize: 1024,
		CheckOrigin:     upgrader.CheckOrigin, // Reuse same origin validation (H5)
		Subprotocols:    []string{"ditto-ws"},
	}
	conn, err := upgraderWithProtocol.Upgrade(c.Writer, c.Request, nil)
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
