package model

import "time"

type Device struct {
	ID         string    `gorm:"primaryKey;size:255" json:"id"`
	UserID     uint      `gorm:"uniqueIndex:idx_user_device_name;not null" json:"user_id"`
	DeviceName string    `gorm:"size:255;not null;uniqueIndex:idx_user_device_name" json:"device_name"`
	LastSeen   time.Time `json:"last_seen"`
	CreatedAt  time.Time `json:"created_at"`

	User User `gorm:"foreignKey:UserID;references:ID" json:"-"`
}

func (Device) TableName() string {
	return "devices"
}
