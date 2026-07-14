package service

import (
	"ditto-cloud-server/internal/database"
	"ditto-cloud-server/internal/model"
)

type StatsService struct{}

func NewStatsService() *StatsService {
	return &StatsService{}
}

func (s *StatsService) GetDeviceStats(userID uint) (int64, error) {
	var clips []model.Clip
	if err := database.DB.Where("user_id = ? AND deleted_at IS NULL", userID).Preload("Formats").Find(&clips).Error; err != nil {
		return 0, err
	}

	var totalStorage int64
	for _, clip := range clips {
		for _, f := range clip.Formats {
			totalStorage += int64(len(f.Data))
		}
	}

	return totalStorage, nil
}