package service

import (
	"errors"
	"ditto-cloud-server/internal/database"
	"ditto-cloud-server/internal/model"
	"ditto-cloud-server/internal/response"

	"gorm.io/gorm"
)

type DeviceService struct{}

func NewDeviceService() *DeviceService {
	return &DeviceService{}
}

type DeviceInfo struct {
	ID         string `json:"id"`
	DeviceName string `json:"device_name"`
	LastSeen   string `json:"last_seen"`
	CreatedAt  string `json:"created_at"`
}

func (s *DeviceService) ListByUser(userID uint, page, perPage int) (*response.PaginatedResponse, error) {
	if page < 1 {
		page = 1
	}
	if perPage < 1 {
		perPage = 20
	}
	if perPage > 100 {
		perPage = 100
	}

	var total int64
	if err := database.DB.Model(&model.Device{}).Where("user_id = ?", userID).Count(&total).Error; err != nil {
		return nil, err
	}

	var devices []model.Device
	if err := database.DB.Where("user_id = ?", userID).Order("last_seen DESC").Offset((page - 1) * perPage).Limit(perPage).Find(&devices).Error; err != nil {
		return nil, err
	}

	items := make([]DeviceInfo, 0, len(devices))
	for _, d := range devices {
		items = append(items, DeviceInfo{
			ID:         d.ID,
			DeviceName: d.DeviceName,
			LastSeen:   d.LastSeen.Format("2006-01-02T15:04:05Z"),
			CreatedAt:  d.CreatedAt.Format("2006-01-02T15:04:05Z"),
		})
	}

	return &response.PaginatedResponse{
		Items:   items,
		Total:   total,
		Page:    page,
		PerPage: perPage,
	}, nil
}

func (s *DeviceService) RemoveDevice(userID uint, deviceID string) error {
	return database.DB.Transaction(func(tx *gorm.DB) error {
		if err := tx.Where("device_id = ? AND user_id = ?", deviceID, userID).Delete(&model.SyncLog{}).Error; err != nil {
			return err
		}
		return tx.Where("id = ? AND user_id = ?", deviceID, userID).Delete(&model.Device{}).Error
	})
}

func (s *DeviceService) RenameDevice(userID uint, deviceID string, newName string) error {
	if newName == "" {
		return errors.New("设备名称不能为空")
	}
	result := database.DB.Model(&model.Device{}).
		Where("id = ? AND user_id = ?", deviceID, userID).
		Update("device_name", newName)
	if result.Error != nil {
		return result.Error
	}
	if result.RowsAffected == 0 {
		return errors.New("设备不存在")
	}
	return nil
}
