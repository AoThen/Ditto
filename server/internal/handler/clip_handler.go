package handler

import (
	"net/http"
	"strconv"

	"ditto-cloud-server/internal/middleware"
	"ditto-cloud-server/internal/response"
	"ditto-cloud-server/internal/service"

	"github.com/gin-gonic/gin"
)

type ClipHandler struct {
	service *service.ClipService
}

func NewClipHandler(svc *service.ClipService) *ClipHandler {
	return &ClipHandler{service: svc}
}

// ListClips handles GET /api/v1/clips
func (h *ClipHandler) ListClips(c *gin.Context) {
	userID := middleware.GetUserID(c)

	page, _ := strconv.Atoi(c.DefaultQuery("page", "1"))
	perPage, _ := strconv.Atoi(c.DefaultQuery("per_page", "20"))
	search := c.Query("search")
	groupID := c.Query("group_id")

	result, err := h.service.ListClips(userID, page, perPage, search, groupID)
	if err != nil {
		response.Error(c, http.StatusInternalServerError, 50000, "获取剪贴板列表失败: "+err.Error())
		return
	}

	response.Success(c, result)
}

// GetClip handles GET /api/v1/clips/:id
func (h *ClipHandler) GetClip(c *gin.Context) {
	userID := middleware.GetUserID(c)
	clipID := c.Param("id")

	result, err := h.service.GetClip(userID, clipID)
	if err != nil {
		if err.Error() == "剪贴板不存在" {
			response.Error(c, http.StatusNotFound, 40400, err.Error())
			return
		}
		response.Error(c, http.StatusInternalServerError, 50000, "获取剪贴板失败: "+err.Error())
		return
	}

	response.Success(c, result)
}

// DeleteClip handles DELETE /api/v1/clips/:id
func (h *ClipHandler) DeleteClip(c *gin.Context) {
	userID := middleware.GetUserID(c)
	clipID := c.Param("id")

	if err := h.service.DeleteClip(userID, clipID); err != nil {
		if err.Error() == "剪贴板不存在" {
			response.Error(c, http.StatusNotFound, 40400, err.Error())
			return
		}
		response.Error(c, http.StatusInternalServerError, 50000, "删除剪贴板失败: "+err.Error())
		return
	}

	response.SuccessWithMessage(c, "剪贴板已删除", nil)
}

// Sync handles POST /api/v1/clips/sync
func (h *ClipHandler) Sync(c *gin.Context) {
	userID := middleware.GetUserID(c)
	deviceID := middleware.GetDeviceID(c)

	var req service.SyncRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		response.Error(c, http.StatusBadRequest, 40000, "请求参数错误: "+err.Error())
		return
	}

	// Use device_id from JWT if not provided in request body
	if req.DeviceID == "" {
		req.DeviceID = deviceID
	}

	result, err := h.service.Sync(userID, &req, deviceID)
	if err != nil {
		service.LogSyncOperation(userID, req.DeviceID, "push", 0, "failed", err.Error())
		response.Error(c, http.StatusInternalServerError, 50000, "同步失败: "+err.Error())
		return
	}

	// Log successful sync
	service.LogSyncOperation(userID, req.DeviceID, "push", result.UpdatedCount, "success", "")

	response.Success(c, result)
}
