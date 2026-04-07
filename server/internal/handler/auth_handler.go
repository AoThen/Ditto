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

	response.Success(c, resp)
}
