package handler

import (
	"net/http"

	"ditto-cloud-server/internal/middleware"
	"ditto-cloud-server/internal/response"
	"ditto-cloud-server/internal/service"

	"github.com/gin-gonic/gin"
)

type AuthHandler struct {
	service    *service.AuthService
	rateLimiter *middleware.RateLimiter
}

func NewAuthHandler(svc *service.AuthService, rl *middleware.RateLimiter) *AuthHandler {
	return &AuthHandler{
		service:    svc,
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

	// H1: Set HttpOnly Secure cookies for browser clients
	// Also include tokens in JSON response for API clients / backward compat
	setAuthCookies(c, resp.DeviceToken, resp.RefreshToken, resp.DeviceID)

	response.Success(c, gin.H{
		"device_token":  resp.DeviceToken,
		"refresh_token": resp.RefreshToken,
		"device_id":     resp.DeviceID,
	})
}

func (h *AuthHandler) Refresh(c *gin.Context) {
	userID := middleware.GetUserID(c)
	deviceID := middleware.GetDeviceID(c)
	oldToken := middleware.GetRawToken(c)

	newToken, refreshToken, err := h.service.RefreshDeviceToken(deviceID, oldToken)
	if err != nil {
		response.Error(c, http.StatusUnauthorized, 40101, "Token 刷新失败: "+err.Error())
		return
	}

	_ = userID
	// H1: Set new HttpOnly cookies
	setAuthCookies(c, newToken, refreshToken, deviceID)

	response.SuccessWithMessage(c, "Token 刷新成功", gin.H{
		"device_id": deviceID,
	})
}

// setAuthCookies sets HttpOnly Secure cookies for authentication tokens.
func setAuthCookies(c *gin.Context, accessToken, refreshToken, deviceID string) {
	// Access token cookie (30 days)
	c.SetCookie("device_token", accessToken, 30*24*3600, "/", "", false, true)
	// Refresh token cookie (90 days)
	c.SetCookie("refresh_token", refreshToken, 90*24*3600, "/", "", false, true)
	// Device ID cookie (readable by JS for display purposes only)
	c.SetCookie("device_id", deviceID, 30*24*3600, "/", "", false, false)
}

// Logout clears auth cookies and returns success.
func (h *AuthHandler) Logout(c *gin.Context) {
	// H1: Clear auth cookies by setting them with maxAge=0
	c.SetCookie("device_token", "", 0, "/", "", false, true)
	c.SetCookie("refresh_token", "", 0, "/", "", false, true)
	c.SetCookie("device_id", "", 0, "/", "", false, false)
	response.SuccessWithMessage(c, "已退出登录", nil)
}
