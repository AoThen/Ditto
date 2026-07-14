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

func (h *EncryptionHandler) GetEncryptionSalt(c *gin.Context) {
	userID := middleware.GetUserID(c)

	result, err := h.service.GetEncryptionSalt(userID)
	if err != nil {
		response.Error(c, http.StatusInternalServerError, 50000, "获取加密信息失败: "+err.Error())
		return
	}

	response.Success(c, result)
}

func (h *EncryptionHandler) GetKeyMaterial(c *gin.Context) {
	userID := middleware.GetUserID(c)

	result, err := h.service.GetKeyMaterial(userID)
	if err != nil {
		if err == service.ErrEncryptionNotSetup {
			response.Error(c, http.StatusNotFound, 40401, err.Error())
			return
		}
		response.Error(c, http.StatusInternalServerError, 50000, "获取密钥材料失败: "+err.Error())
		return
	}

	response.Success(c, result)
}

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

func (h *EncryptionHandler) ChangeEncryptionPassword(c *gin.Context) {
	userID := middleware.GetUserID(c)

	var req service.ChangePasswordRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		response.Error(c, http.StatusBadRequest, 40000, "请求参数错误: "+err.Error())
		return
	}

	result, err := h.service.ChangeEncryptionPassword(userID, &req)
	if err != nil {
		if err == service.ErrEncryptionNotSetup {
			response.Error(c, http.StatusNotFound, 40401, err.Error())
			return
		}
		if err == service.ErrInvalidVerificationHash {
			response.Error(c, http.StatusForbidden, 40301, err.Error())
			return
		}
		response.Error(c, http.StatusInternalServerError, 50000, "修改加密密码失败: "+err.Error())
		return
	}

	response.SuccessWithMessage(c, "加密密码已更新", result)
}