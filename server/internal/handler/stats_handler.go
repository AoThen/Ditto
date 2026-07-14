package handler

import (
	"log"
	"net/http"
	"strconv"
	"time"

	"ditto-cloud-server/internal/database"
	"ditto-cloud-server/internal/middleware"
	"ditto-cloud-server/internal/model"
	"ditto-cloud-server/internal/response"
	"ditto-cloud-server/internal/service"

	"github.com/gin-gonic/gin"
)

// StatsHandler handles statistics endpoints
type StatsHandler struct {
	service *service.StatsService
}

// NewStatsHandler creates a new stats handler
func NewStatsHandler(svc *service.StatsService) *StatsHandler {
	return &StatsHandler{service: svc}
}

// GetOverview returns statistics overview
func (h *StatsHandler) GetOverview(c *gin.Context) {
	userID := middleware.GetUserID(c)

	overview, err := h.service.GetOverview(userID)
	if err != nil {
		log.Printf("[GetOverview] error: %v", err)
		response.Error(c, http.StatusInternalServerError, 50000, "获取统计数据失败")
		return
	}

	response.Success(c, overview)
}

// GetSyncLogs returns sync logs for the user
func (h *StatsHandler) GetSyncLogs(c *gin.Context) {
	userID := middleware.GetUserID(c)

	page, _ := strconv.Atoi(c.DefaultQuery("page", "1"))
	perPage, _ := strconv.Atoi(c.DefaultQuery("per_page", "20"))
	deviceID := c.Query("device_id")
	action := c.Query("action")

	if page < 1 {
		page = 1
	}
	if perPage < 1 {
		perPage = 20
	}
	if perPage > 100 {
		perPage = 100
	}

	query := database.DB.Model(&model.SyncLog{}).Where("user_id = ?", userID)

	if deviceID != "" {
		query = query.Where("device_id = ?", deviceID)
	}
	if action != "" {
		query = query.Where("action = ?", action)
	}

	var total int64
	if err := query.Count(&total).Error; err != nil {
		log.Printf("[GetSyncLogs] count error: %v", err)
		response.Error(c, http.StatusInternalServerError, 50000, "获取同步日志失败")
		return
	}

	var logs []model.SyncLog
	if err := query.Order("synced_at DESC").Offset((page - 1) * perPage).Limit(perPage).Find(&logs).Error; err != nil {
		log.Printf("[GetSyncLogs] find error: %v", err)
		response.Error(c, http.StatusInternalServerError, 50000, "获取同步日志失败")
		return
	}

	type LogItem struct {
		ID        uint   `json:"id"`
		DeviceID  string `json:"device_id"`
		Action    string `json:"action"`
		ClipCount int    `json:"clip_count"`
		Status    string `json:"status"`
		Error     string `json:"error"`
		SyncedAt  string `json:"synced_at"`
	}

	items := make([]LogItem, 0, len(logs))
	for _, log := range logs {
		items = append(items, LogItem{
			ID:        log.ID,
			DeviceID:  log.DeviceID,
			Action:    log.Action,
			ClipCount: log.ClipCount,
			Status:    log.Status,
			Error:     log.Error,
			SyncedAt:  log.SyncedAt.UTC().Format(time.RFC3339),
		})
	}

	response.Success(c, response.PaginatedResponse{
		Items:   items,
		Total:   total,
		Page:    page,
		PerPage: perPage,
	})
}
