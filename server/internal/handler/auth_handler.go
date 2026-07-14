package handler

import (
	"net/http"

	"ditto-cloud-server/internal/middleware"
	"ditto-cloud-server/internal/response"
	"ditto-cloud-server/internal/service"

	"github.com/gin-gonic/gin"
)

type AuthHandler struct {
	service     *service.AuthService
	rateLimiter *middleware.RateLimiter
}

func NewAuthHandler(svc *service.AuthService, rl *middleware.RateLimiter) *AuthHandler {
	return &AuthHandler{
		service:     svc,
		rateLimiter: rl,
	}
}

func (h *AuthHandler) Register(c *gin.Context) {
	var req service.RegisterRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		response.Error(c, http.StatusBadRequest, 40000, "请求参数错误: "+err.Error())
		return
	}

	resp, err := h.service.Register(&req)
	if err != nil {
		switch err {
		case service.ErrUsernameExists:
			response.Error(c, http.StatusBadRequest, 40001, "用户名已存在")
		case service.ErrEmailExists:
			response.Error(c, http.StatusBadRequest, 40002, "邮箱已被注册")
		default:
			response.Error(c, http.StatusInternalServerError, 50000, "注册失败: "+err.Error())
		}
		return
	}

	response.SuccessWithMessage(c, "注册成功", resp)
}

func (h *AuthHandler) Login(c *gin.Context) {
	var req service.LoginRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		response.Error(c, http.StatusBadRequest, 40000, "请求参数错误: "+err.Error())
		return
	}

	// Check if user account is locked
	if h.rateLimiter.IsUserLocked(req.Username) {
		response.Error(c, http.StatusLocked, 42301, "账号已锁定，请 1 小时后重试")
		return
	}

	resp, err := h.service.Login(&req, c.GetHeader("X-Device-Name"))
	if err != nil {
		switch err {
		case service.ErrInvalidCreds:
			// Record failure for rate limiting
			h.rateLimiter.RecordLoginFailure(c.ClientIP(), req.Username)
			response.Error(c, http.StatusUnauthorized, 40101, "用户名或密码错误")
		default:
			response.Error(c, http.StatusInternalServerError, 50000, "登录失败: "+err.Error())
		}
		return
	}

	// Record successful login (reset rate limits)
	h.rateLimiter.RecordLoginSuccess(c.ClientIP(), req.Username)

	// H1+H2: Set HttpOnly Secure SameSite cookies for browser clients
	// Also include tokens in JSON response for API clients / backward compat
	setAuthCookies(c, resp.DeviceToken, resp.RefreshToken, resp.DeviceID, h.service)

	response.Success(c, gin.H{
		"message": "登录成功",
	})
}

func (h *AuthHandler) Refresh(c *gin.Context) {
	userID := middleware.GetUserID(c)
	deviceID := middleware.GetDeviceID(c)

	newToken, refreshToken, err := h.service.RefreshDeviceToken(userID, deviceID)
	if err != nil {
		response.Error(c, http.StatusUnauthorized, 40101, "Token 刷新失败: "+err.Error())
		return
	}

	// H1+H2: Set new Secure SameSite cookies
	setAuthCookies(c, newToken, refreshToken, deviceID, h.service)

	response.SuccessWithMessage(c, "Token 刷新成功", gin.H{
		"device_id": deviceID,
	})
}

// setAuthCookies sets HttpOnly Secure SameSite cookies for authentication tokens.
// H1 FIX: Secure flag from config (default true for production)
// H2 FIX: SameSite=Lax via http.SetCookie (Gin c.SetCookie doesn't support SameSite)
func setAuthCookies(c *gin.Context, accessToken, refreshToken, deviceID string, svc *service.AuthService) {
	// Determine max-age from config
	accessMaxAge := int(svc.GetTokenExpiryAccess().Seconds())
	refreshMaxAge := int(svc.GetTokenExpiryRefresh().Seconds())
	secure := svc.IsCookieSecure()

	// Access token cookie
	http.SetCookie(c.Writer, &http.Cookie{
		Name:     "device_token",
		Value:    accessToken,
		MaxAge:   accessMaxAge,
		Path:     "/",
		Secure:   secure,
		HttpOnly: true,
		SameSite: http.SameSiteLaxMode,
	})

	// Refresh token cookie
	http.SetCookie(c.Writer, &http.Cookie{
		Name:     "refresh_token",
		Value:    refreshToken,
		MaxAge:   refreshMaxAge,
		Path:     "/",
		Secure:   secure,
		HttpOnly: true,
		SameSite: http.SameSiteLaxMode,
	})

	// Device ID cookie (readable by JS for display purposes only)
	http.SetCookie(c.Writer, &http.Cookie{
		Name:     "device_id",
		Value:    deviceID,
		MaxAge:   accessMaxAge,
		Path:     "/",
		Secure:   secure,
		HttpOnly: false,
		SameSite: http.SameSiteLaxMode,
	})
}

// Logout clears auth cookies and returns success.
func (h *AuthHandler) Logout(c *gin.Context) {
	secure := h.service.IsCookieSecure()

	// Clear auth cookies by setting them with maxAge=0
	http.SetCookie(c.Writer, &http.Cookie{
		Name: "device_token", MaxAge: -1, Path: "/", Secure: secure, HttpOnly: true, SameSite: http.SameSiteLaxMode,
	})
	http.SetCookie(c.Writer, &http.Cookie{
		Name: "refresh_token", MaxAge: -1, Path: "/", Secure: secure, HttpOnly: true, SameSite: http.SameSiteLaxMode,
	})
	http.SetCookie(c.Writer, &http.Cookie{
		Name: "device_id", MaxAge: -1, Path: "/", Secure: secure, HttpOnly: false, SameSite: http.SameSiteLaxMode,
	})
	response.SuccessWithMessage(c, "已退出登录", nil)
}
