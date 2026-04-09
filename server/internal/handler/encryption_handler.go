package handler

import (
	"net/http"

	"ditto-cloud-server/internal/middleware"
	"ditto-cloud-server/internal/response"
	"ditto-cloud-server/internal/service"

	"github.com/gin-gonic/gin"
)

type EncryptionHandler struct {
	service *service.EncryptionService
}

func NewEncryptionHandler(svc *service.EncryptionService) *EncryptionHandler {
	return &EncryptionHandler{service: svc}
}

// SetupEncryption handles POST /api/v1/encryption/setup
func (h *EncryptionHandler) SetupEncryption(c *gin.Context) {
	userID := middleware.GetUserID(c)

	var req service.SetupEncryptionRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		response.Error(c, http.StatusBadRequest, 40000, "请求参数错误: "+err.Error())
		return
	}

	result, err := h.service.SetupEncryption(userID, &req)
	if err != nil {
		if err == service.ErrEncryptionAlreadyEnabled {
			response.Error(c, http.StatusConflict, 40901, err.Error())
			return
		}
		response.Error(c, http.StatusInternalServerError, 50000, "设置加密失败: "+err.Error())
		return
	}

	response.SuccessWithMessage(c, "加密已启用", result)
}

// GetEncryptionSalt handles GET /api/v1/encryption/salt
func (h *EncryptionHandler) GetEncryptionSalt(c *gin.Context) {
	userID := middleware.GetUserID(c)

	result, err := h.service.GetEncryptionSalt(userID)
	if err != nil {
		if err == service.ErrEncryptionNotSetup {
			response.Error(c, http.StatusNotFound, 40401, err.Error())
			return
		}
		response.Error(c, http.StatusInternalServerError, 50000, "获取加密信息失败: "+err.Error())
		return
	}

	response.Success(c, result)
}

// DisableEncryption handles POST /api/v1/encryption/disable
func (h *EncryptionHandler) DisableEncryption(c *gin.Context) {
	userID := middleware.GetUserID(c)

	err := h.service.DisableEncryption(userID)
	if err != nil {
		if err == service.ErrEncryptionNotSetup {
			response.Error(c, http.StatusNotFound, 40401, err.Error())
			return
		}
		response.Error(c, http.StatusInternalServerError, 50000, "禁用加密失败: "+err.Error())
		return
	}

	response.SuccessWithMessage(c, "加密已禁用", nil)
}

// ChangeEncryptionPassword handles POST /api/v1/encryption/change-password
func (h *EncryptionHandler) ChangeEncryptionPassword(c *gin.Context) {
	userID := middleware.GetUserID(c)

	var req struct {
		PasswordHint string `json:"password_hint" binding:"required"`
	}
	if err := c.ShouldBindJSON(&req); err != nil {
		response.Error(c, http.StatusBadRequest, 40000, "请求参数错误: "+err.Error())
		return
	}

	err := h.service.ChangeEncryptionPassword(userID, req.PasswordHint)
	if err != nil {
		if err == service.ErrEncryptionNotSetup {
			response.Error(c, http.StatusNotFound, 40401, err.Error())
			return
		}
		response.Error(c, http.StatusInternalServerError, 50000, "修改密码提示失败: "+err.Error())
		return
	}

	response.SuccessWithMessage(c, "密码提示已更新", nil)
}
