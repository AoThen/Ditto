package service

import (
	"time"

	"ditto-cloud-server/internal/database"
)

type StatsService struct{}

func NewStatsService() *StatsService {
	return &StatsService{}
}

func (s *StatsService) GetDeviceStats(userID uint) (int64, error) {
	var totalStorage int64
	err := database.DB.Raw(
		`SELECT COALESCE(SUM(LENGTH(data)), 0) FROM clip_formats WHERE clip_id IN (SELECT id FROM clips WHERE user_id = ? AND deleted_at IS NULL)`,
		userID,
	).Scan(&totalStorage).Error
	return totalStorage, err
}

type OverviewResponse struct {
	TotalClips    int64         `json:"total_clips"`
	TodayClips    int64         `json:"today_clips"`
	TotalDevices  int64         `json:"total_devices"`
	TotalStorage  int64         `json:"total_storage"`
	StorageMb     float64       `json:"storage_mb"`
	MaxStorageMb  int           `json:"max_storage_mb"`
	Trend         []DayCount    `json:"trend"`
}

type DayCount struct {
	Date  string `json:"date"`
	Count int64  `json:"count"`
}

func (s *StatsService) GetOverview(userID uint) (*OverviewResponse, error) {
	var totalClips int64
	var todayClips int64
	var totalDevices int64
	var totalStorage int64

	database.DB.Raw("SELECT COUNT(*) FROM clips WHERE user_id = ? AND deleted_at IS NULL", userID).Scan(&totalClips)

	now := time.Now()
	todayStart := time.Date(now.Year(), now.Month(), now.Day(), 0, 0, 0, 0, now.Location())
	database.DB.Raw("SELECT COUNT(*) FROM clips WHERE user_id = ? AND created_at >= ? AND deleted_at IS NULL", userID, todayStart).Scan(&todayClips)

	database.DB.Raw("SELECT COUNT(*) FROM devices WHERE user_id = ?", userID).Scan(&totalDevices)

	storage, err := s.GetDeviceStats(userID)
	if err != nil {
		return nil, err
	}
	totalStorage = storage

	var trend []DayCount
	database.DB.Raw(`
		SELECT DATE(created_at) as date, COUNT(*) as count
		FROM clips
		WHERE user_id = ? AND created_at >= ? AND deleted_at IS NULL
		GROUP BY DATE(created_at)
		ORDER BY date DESC
		LIMIT 7
	`, userID, time.Now().AddDate(0, 0, -7)).Scan(&trend)

	if trend == nil {
		trend = make([]DayCount, 0)
	} else {
		for i, j := 0, len(trend)-1; i < j; i, j = i+1, j-1 {
			trend[i], trend[j] = trend[j], trend[i]
		}
	}

	return &OverviewResponse{
		TotalClips:    totalClips,
		TodayClips:    todayClips,
		TotalDevices:  totalDevices,
		TotalStorage:  totalStorage,
		StorageMb:     float64(totalStorage) / 1024 / 1024,
		MaxStorageMb:  100,
		Trend:         trend,
	}, nil
}