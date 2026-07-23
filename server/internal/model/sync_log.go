package model

import "time"

type SyncLog struct {
	ID        uint      `gorm:"primaryKey;autoIncrement" json:"id"`
	UserID    uint      `gorm:"index:idx_sync_logs_user_time;not null" json:"user_id"`
	DeviceID  string    `gorm:"size:255;index" json:"device_id"`
	Action    string    `gorm:"size:50;not null" json:"action"` // push | pull | delete
	ClipCount int       `gorm:"default:0" json:"clip_count"`
	Status    string    `gorm:"size:20;default:success" json:"status"` // success | failed | conflict
	Error     string    `gorm:"type:text" json:"error"`
	SyncedAt  time.Time `gorm:"index:idx_sync_logs_user_time" json:"synced_at"`

	User User `gorm:"foreignKey:UserID;references:ID" json:"-"`
}

func (SyncLog) TableName() string {
	return "sync_logs"
}
