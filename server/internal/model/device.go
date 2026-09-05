package model

import "time"

type Device struct {
	ID     string `gorm:"primaryKey;size:255" json:"id"`
	UserID uint   `gorm:"index;not null" json:"user_id"`
	// DeviceName is a label only. It used to be part of a unique
	// (user_id, device_name) index, which made a second device with the same
	// name fail to register at all.
	DeviceName string    `gorm:"size:255;not null" json:"device_name"`
	LastSeen   time.Time `json:"last_seen"`
	CreatedAt  time.Time `json:"created_at"`
	TokenVersion int    `gorm:"default:0" json:"token_version"`

	User User `gorm:"foreignKey:UserID;references:ID" json:"-"`
}

func (Device) TableName() string {
	return "devices"
}
