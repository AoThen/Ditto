package service

import (
	"ditto-cloud-server/internal/database"
	"ditto-cloud-server/internal/model"

	"gorm.io/gorm"
)

type DeviceService struct{}

func NewDeviceService() *DeviceService {
	return &DeviceService{}
}

type DeviceInfo struct {
	ID        string `json:"id"`
	DeviceName string `json:"device_name"`
	LastSeen  string `json:"last_seen"`
}

func (s *DeviceService) ListByUser(userID uint) ([]DeviceInfo, error) {
	var devices []model.Device
	if err := database.DB.Where("user_id = ?", userID).Order("last_seen DESC").Find(&devices).Error; err != nil {
		return nil, err
	}

	result := make([]DeviceInfo, 0, len(devices))
	for _, d := range devices {
		result = append(result, DeviceInfo{
			ID:        d.ID,
			DeviceName: d.DeviceName,
			LastSeen:  d.LastSeen.Format("2006-01-02T15:04:05Z"),
		})
	}
	return result, nil
}

func (s *DeviceService) RemoveDevice(userID uint, deviceID string) error {
	return database.DB.Transaction(func(tx *gorm.DB) error {
		if err := tx.Where("device_id = ? AND user_id = ?", deviceID, userID).Delete(&model.SyncLog{}).Error; err != nil {
			return err
		}
		return tx.Where("id = ? AND user_id = ?", deviceID, userID).Delete(&model.Device{}).Error
	})
}
