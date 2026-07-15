package handler

import (
	"errors"
	"fmt"
	"log"
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
	sortBy := c.Query("sort_by")
	sortOrder := c.Query("sort_order")

	result, err := h.service.ListClips(userID, page, perPage, search, groupID, sortBy, sortOrder)
	if err != nil {
		if errors.Is(err, service.ErrInvalidSortBy) {
			response.Error(c, http.StatusBadRequest, 40000, "无效的排序参数")
			return
		}
		log.Printf("[ListClips] error: %v", err)
		response.Error(c, http.StatusInternalServerError, 50000, "获取剪贴板列表失败")
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
		if errors.Is(err, service.ErrClipNotFound) {
			response.Error(c, http.StatusNotFound, 40400, "剪贴板不存在")
			return
		}
		log.Printf("[GetClip] error: %v", err)
		response.Error(c, http.StatusInternalServerError, 50000, "获取剪贴板失败")
		return
	}

	response.Success(c, result)
}

// DeleteClip handles DELETE /api/v1/clips/:id
func (h *ClipHandler) DeleteClip(c *gin.Context) {
	userID := middleware.GetUserID(c)
	clipID := c.Param("id")

	if err := h.service.DeleteClip(userID, clipID); err != nil {
		if errors.Is(err, service.ErrClipNotFound) {
			response.Error(c, http.StatusNotFound, 40400, "剪贴板不存在")
			return
		}
		log.Printf("[DeleteClip] error: %v", err)
		response.Error(c, http.StatusInternalServerError, 50000, "删除剪贴板失败")
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
		log.Printf("[Sync] invalid request: %v", err)
		response.Error(c, http.StatusBadRequest, 40000, "请求参数错误")
		return
	}

	// Use device_id from JWT if not provided in request body
	if req.DeviceID == "" {
		req.DeviceID = deviceID
	}

	result, err := h.service.Sync(userID, &req, deviceID)
	if err != nil {
		service.LogSyncOperation(userID, req.DeviceID, "push", 0, "failed", err.Error())
		log.Printf("[Sync] error: %v", err)
		response.Error(c, http.StatusInternalServerError, 50000, "同步失败")
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
		if errors.Is(err, service.ErrClipNotFound) {
			response.Error(c, http.StatusNotFound, 40400, "剪贴板不存在")
			return
		}
		if errors.Is(err, service.ErrFormatNotFound) {
			response.Error(c, http.StatusNotFound, 40401, "指定格式不存在")
			return
		}
		log.Printf("[DownloadClip] error: %v", err)
		response.Error(c, http.StatusInternalServerError, 50000, "下载失败")
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
		log.Printf("[GetChanges] invalid since parameter: %v", err)
		response.Error(c, http.StatusBadRequest, 40000, "无效的 since 参数")
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
		log.Printf("[GetChanges] error: %v", err)
		response.Error(c, http.StatusInternalServerError, 50000, "增量同步失败")
		return
	}

	service.LogSyncOperation(userID, deviceID, "pull", len(result.NewClips), "success", "")

	// Build lightweight response for pull-only
	type ChangesResponse struct {
		Clips      []service.ClipDetail `json:"clips"`
		ServerTime string               `json:"server_time"`
		HasMore    bool                 `json:"has_more"`
		DeletedIDs []string             `json:"deleted_ids"`
	}

	response.Success(c, ChangesResponse{
		Clips:      result.NewClips,
		ServerTime: result.SyncTime,
		HasMore:    result.HasMore,
		DeletedIDs: result.DeletedIDs,
	})
}

// ListConflictClips handles GET /api/v1/clips/conflicts
func (h *ClipHandler) ListConflictClips(c *gin.Context) {
	userID := middleware.GetUserID(c)

	page, _ := strconv.Atoi(c.DefaultQuery("page", "1"))
	perPage, _ := strconv.Atoi(c.DefaultQuery("per_page", "20"))

	result, err := h.service.ListConflictClips(userID, page, perPage)
	if err != nil {
		log.Printf("[ListConflictClips] error: %v", err)
		response.Error(c, http.StatusInternalServerError, 50000, "获取冲突剪贴板失败")
		return
	}

	response.Success(c, result)
}

// ResolveConflictClip handles POST /api/v1/clips/conflicts/:id/resolve
func (h *ClipHandler) ResolveConflictClip(c *gin.Context) {
	userID := middleware.GetUserID(c)
	conflictClipID := c.Param("id")

	var req struct {
		Action string `json:"action" binding:"required"` // "accept" or "discard"
	}
	if err := c.ShouldBindJSON(&req); err != nil {
		log.Printf("[ResolveConflictClip] invalid request: %v", err)
		response.Error(c, http.StatusBadRequest, 40000, "请求参数错误")
		return
	}

	if req.Action != "accept" && req.Action != "discard" {
		response.Error(c, http.StatusBadRequest, 40000, "无效的操作类型，必须是 accept 或 discard")
		return
	}

	if err := h.service.ResolveConflictClip(userID, conflictClipID, req.Action); err != nil {
		if errors.Is(err, service.ErrConflictClipNotFound) {
			response.Error(c, http.StatusNotFound, 40400, "冲突剪贴板不存在")
			return
		}
		log.Printf("[ResolveConflictClip] error: %v", err)
		response.Error(c, http.StatusInternalServerError, 50000, "处理冲突失败")
		return
	}

	response.SuccessWithMessage(c, "冲突已处理", nil)
}

// BatchDeleteClips handles POST /api/v1/clips/batch-delete
func (h *ClipHandler) BatchDeleteClips(c *gin.Context) {
	userID := middleware.GetUserID(c)

	var req struct {
		IDs []string `json:"ids" binding:"required"`
	}
	if err := c.ShouldBindJSON(&req); err != nil {
		log.Printf("[BatchDeleteClips] invalid request: %v", err)
		response.Error(c, http.StatusBadRequest, 40000, "请求参数错误")
		return
	}

	if len(req.IDs) == 0 {
		response.Error(c, http.StatusBadRequest, 40000, "请至少选择一条记录")
		return
	}

	deleted, err := h.service.BatchDeleteClips(userID, req.IDs)
	if err != nil {
		log.Printf("[BatchDeleteClips] error: %v", err)
		response.Error(c, http.StatusInternalServerError, 50000, "批量删除失败")
		return
	}

	response.SuccessWithMessage(c, fmt.Sprintf("成功删除 %d 个剪贴板", deleted), gin.H{"deleted": deleted})
}
