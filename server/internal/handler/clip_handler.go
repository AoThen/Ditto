package handler

import (
	"net/http"
	"strconv"
	"time"

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

// DownloadClip handles GET /api/v1/clips/:id/download
// Returns raw binary data for a specific format (default: CF_UNICODETEXT=13)
func (h *ClipHandler) DownloadClip(c *gin.Context) {
	userID := middleware.GetUserID(c)
	clipID := c.Param("id")

	formatType, _ := strconv.Atoi(c.DefaultQuery("format_type", "13"))

	result, err := h.service.DownloadClipFormat(userID, clipID, formatType)
	if err != nil {
		if err.Error() == "剪贴板不存在" {
			response.Error(c, http.StatusNotFound, 40400, err.Error())
			return
		}
		if err.Error() == "指定格式不存在" {
			response.Error(c, http.StatusNotFound, 40401, err.Error())
			return
		}
		response.Error(c, http.StatusInternalServerError, 50000, "下载失败: "+err.Error())
		return
	}

	// Set headers for file download
	c.Header("Content-Disposition", "attachment; filename="+result.FileName)
	c.Header("Content-Type", result.ContentType)
	c.Header("Content-Length", strconv.Itoa(len(result.Data)))
	c.Data(http.StatusOK, result.ContentType, result.Data)
}

// GetChanges handles GET /api/v1/clips/changes (incremental sync)
func (h *ClipHandler) GetChanges(c *gin.Context) {
	userID := middleware.GetUserID(c)
	deviceID := middleware.GetDeviceID(c)

	sinceStr := c.Query("since")
	if sinceStr == "" {
		// Default to epoch if not specified
		sinceStr = "1970-01-01T00:00:00Z"
	}
	since, err := time.Parse(time.RFC3339, sinceStr)
	if err != nil {
		response.Error(c, http.StatusBadRequest, 40000, "无效的 since 参数: "+err.Error())
		return
	}

	// Reuse Sync service for pull-only: empty push_clips
	req := &service.SyncRequest{
		Since:     since,
		DeviceID:  deviceID,
		PushClips: []service.PushClipItem{}, // No push, just pull
	}

	result, err := h.service.Sync(userID, req, deviceID)
	if err != nil {
		service.LogSyncOperation(userID, deviceID, "pull", 0, "failed", err.Error())
		response.Error(c, http.StatusInternalServerError, 50000, "增量同步失败: "+err.Error())
		return
	}

	service.LogSyncOperation(userID, deviceID, "pull", len(result.NewClips), "success", "")

	// Build lightweight response for pull-only
	type ChangesResponse struct {
		Clips       []service.ClipDetail `json:"clips"`
		ServerTime  string               `json:"server_time"`
		HasMore     bool                 `json:"has_more"`
		DeletedIDs  []string             `json:"deleted_ids"`
	}

	response.Success(c, ChangesResponse{
		Clips:      result.NewClips,
		ServerTime: result.SyncTime,
		HasMore:    false,
		DeletedIDs: []string{},
	})
}
