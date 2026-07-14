package middleware

import (
	"net/http"
	"strings"

	"ditto-cloud-server/internal/config"
	"ditto-cloud-server/internal/database"
	"ditto-cloud-server/internal/model"
	"ditto-cloud-server/internal/response"

	"github.com/gin-gonic/gin"
	"github.com/golang-jwt/jwt/v5"
)

type Claims struct {
	UserID       uint   `json:"user_id"`
	DeviceID     string `json:"device_id"`
	TokenVersion int    `json:"token_version"`
	jwt.RegisteredClaims
}

func Auth(cfg *config.Config) gin.HandlerFunc {
	return func(c *gin.Context) {
		// HIGH FIX (H1): Read JWT from HttpOnly cookie as primary source,
		// with Authorization header fallback for backward compatibility
		tokenStr := ""

		// Try HttpOnly cookie first (more secure than Authorization header)
		if cookie, err := c.Cookie("device_token"); err == nil && cookie != "" {
			tokenStr = cookie
		} else {
			// Fallback to Authorization header (for API clients, tests, etc.)
			authHeader := c.GetHeader("Authorization")
			if authHeader != "" {
				tokenStr = strings.TrimPrefix(authHeader, "Bearer ")
			}
		}

		if tokenStr == "" {
			response.Error(c, http.StatusUnauthorized, 40100, "未提供认证令牌")
			c.Abort()
			return
		}

		token, err := jwt.ParseWithClaims(tokenStr, &Claims{}, func(t *jwt.Token) (interface{}, error) {
			// MEDIUM FIX (M6): Enforce HMAC algorithm to prevent alg=None / algorithm confusion attacks
			if _, ok := t.Method.(*jwt.SigningMethodHMAC); !ok {
				return nil, jwt.ErrSignatureInvalid
			}
			return []byte(cfg.JWTSecret), nil
		})
		if err != nil || !token.Valid {
			response.Error(c, http.StatusUnauthorized, 40102, "无效的认证令牌")
			c.Abort()
			return
		}

		claims, ok := token.Claims.(*Claims)
		if !ok {
			response.Error(c, http.StatusUnauthorized, 40102, "无效的认证令牌")
			c.Abort()
			return
		}

		// Verify token_version against database to support refresh token rotation
		var device model.Device
		if err := database.DB.Where("id = ?", claims.DeviceID).First(&device).Error; err != nil {
			response.Error(c, http.StatusUnauthorized, 40102, "设备不存在")
			c.Abort()
			return
		}
		if claims.TokenVersion != device.TokenVersion {
			response.Error(c, http.StatusUnauthorized, 40102, "令牌已过期，请重新登录")
			c.Abort()
			return
		}

		c.Set("user_id", claims.UserID)
		c.Set("device_id", claims.DeviceID)
		// C3 FIX: Store raw token in context for refresh (avoids ParseUnverified)
		c.Set("raw_token", tokenStr)
		c.Next()
	}
}

func GetUserID(c *gin.Context) uint {
	v, exists := c.Get("user_id")
	if !exists {
		return 0
	}
	return v.(uint)
}

func GetDeviceID(c *gin.Context) string {
	v, exists := c.Get("device_id")
	if !exists {
		return ""
	}
	return v.(string)
}

// GetRawToken returns the raw JWT token string that was authenticated by the middleware.
// C3 FIX: Reads from context (set by Auth middleware) instead of re-parsing headers.
// This ensures consistency with the token that was actually verified.
func GetRawToken(c *gin.Context) string {
	v, exists := c.Get("raw_token")
	if !exists {
		return ""
	}
	return v.(string)
}
