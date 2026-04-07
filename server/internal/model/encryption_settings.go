package model

import "time"

// EncryptionSettings stores per-user encryption configuration
type EncryptionSettings struct {
	ID              uint      `gorm:"primaryKey;autoIncrement" json:"id"`
	UserID          uint      `gorm:"uniqueIndex;not null" json:"user_id"`
	Salt            []byte    `gorm:"size:32;not null" json:"-"`
	PasswordHint    string    `gorm:"type:text" json:"password_hint"`
	Enabled         bool      `gorm:"default:false" json:"encryption_enabled"`
	CreatedAt       time.Time `json:"created_at"`
	UpdatedAt       time.Time `json:"updated_at"`

	User User `gorm:"foreignKey:UserID;references:ID" json:"-"`
}

func (EncryptionSettings) TableName() string {
	return "encryption_settings"
}
