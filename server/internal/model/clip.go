package model

import "time"

// FormatType constants for clipboard format types (CF_ values from Windows)
const (
	FormatTypeText            = 1   // CF_TEXT
	FormatTypeUnicodeText     = 13  // CF_UNICODETEXT
	FormatTypeFileGroup       = 15  // CF_HDROP (file paths)
	FormatTypeDib             = 8   // CF_DIB (image)
	FormatTypeDibV5           = 17  // CF_DIBV5
	FormatTypeHtml            = 49  // Custom HTML format
	FormatTypeImage           = 50  // Custom image format
	FormatTypeOwnerLink       = 128 // CF_OWNERDISPLAY
	FormatTypeOemText         = 7   // CF_OEMTEXT
)

type Clip struct {
	ID             string    `gorm:"primaryKey;size:255" json:"id"`
	UserID         uint      `gorm:"index:idx_clips_user_created;not null" json:"user_id"`
	DeviceID       string    `gorm:"size:255" json:"device_id"`
	Description    string    `gorm:"type:text" json:"description"`
	CRC            int64     `gorm:"index:idx_clips_user_crc" json:"crc"`
	CreatedAt      time.Time `json:"created_at"`
	UpdatedAt      time.Time `json:"updated_at"`
	GroupID        string    `gorm:"size:255" json:"group_id"`
	ShortCut       int       `gorm:"default:0" json:"short_cut"`
	PasteCount     int       `gorm:"default:0" json:"paste_count"`
	IsConflictCopy bool      `gorm:"index:idx_clips_user_conflict;default:false" json:"is_conflict_copy"` // LWW losing clip kept for review

	User   User   `gorm:"foreignKey:UserID;references:ID" json:"-"`
	Device Device `gorm:"foreignKey:DeviceID;references:ID" json:"-"`
}

func (Clip) TableName() string {
	return "clips"
}
