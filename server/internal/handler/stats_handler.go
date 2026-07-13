package handler

import (
	"net/http"
	"strconv"
	"time"

	"ditto-cloud-server/internal/database"
	"ditto-cloud-server/internal/middleware"
	"ditto-cloud-server/internal/model"
	"ditto-cloud-server/internal/response"

	"github.com/gin-gonic/gin"
)

// StatsHandler handles statistics endpoints
type StatsHandler struct{}

// NewStatsHandler creates a new stats handler
func NewStatsHandler() *StatsHandler {
	return &StatsHandler{}
}

// GetOverview returns statistics overview
func (h *StatsHandler) GetOverview(c *gin.Context) {
	userID := middleware.GetUserID(c)

	var totalClips int64
	var todayClips int64
	var totalDevices int64
	var totalStorage int64

	// Get total clips count
	database.DB.Model(&model.Clip{}).Where("user_id = ? AND deleted_at IS NULL", userID).Count(&totalClips)

	// Get today's clips count
	now := time.Now()
	todayStart := time.Date(now.Year(), now.Month(), now.Day(), 0, 0, 0, 0, now.Location())
	database.DB.Model(&model.Clip{}).Where("user_id = ? AND created_at >= ? AND deleted_at IS NULL", userID, todayStart).Count(&todayClips)

	// Get devices count
	database.DB.Model(&model.Device{}).Where("user_id = ?", userID).Count(&totalDevices)

	// Get total storage (sum of all format data sizes)
	var clips []model.Clip
	database.DB.Where("user_id = ? AND deleted_at IS NULL", userID).Find(&clips)
	
	for _, clip := range clips {
		var formats []model.ClipFormat
		database.DB.Where("clip_id = ?", clip.ID).Find(&formats)
		for _, fmt := range formats {
			totalStorage += int64(len(fmt.Data))
		}
	}

	// Get last 7 days trend
	type DayCount struct {
		Date  string
		Count int64
	}

	var trend []DayCount
	database.DB.Raw(`
		SELECT DATE(created_at) as date, COUNT(*) as count
		FROM clips
		WHERE user_id = ? AND created_at >= ? AND deleted_at IS NULL
		GROUP BY DATE(created_at)
		ORDER BY date DESC
		LIMIT 7
	`, userID, time.Now().AddDate(0, 0, -7)).Scan(&trend)

	// Reverse trend to get ascending order
	for i, j := 0, len(trend)-1; i < j; i, j = i+1, j-1 {
		trend[i], trend[j] = trend[j], trend[i]
	}

	response.Success(c, gin.H{
		"total_clips":    totalClips,
		"today_clips":    todayClips,
		"total_devices":  totalDevices,
		"total_storage":  totalStorage,
		"storage_mb":     float64(totalStorage) / 1024 / 1024,
		"max_storage_mb": 100, // From user settings
		"trend":          trend,
	})
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
		response.Error(c, http.StatusInternalServerError, 50000, "获取同步日志失败: "+err.Error())
		return
	}

	var logs []model.SyncLog
	if err := query.Order("synced_at DESC").Offset((page - 1) * perPage).Limit(perPage).Find(&logs).Error; err != nil {
		response.Error(c, http.StatusInternalServerError, 50000, "获取同步日志失败: "+err.Error())
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
