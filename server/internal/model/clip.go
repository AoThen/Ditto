package model

import (
	"time"

	"gorm.io/gorm"
)

// FormatType constants for clipboard format types (CF_ values from Windows)
const (
	FormatTypeText        = 1   // CF_TEXT
	FormatTypeUnicodeText = 13  // CF_UNICODETEXT
	FormatTypeFileGroup   = 15  // CF_HDROP (file paths)
	FormatTypeDib         = 8   // CF_DIB (image)
	FormatTypeDibV5       = 17  // CF_DIBV5
	FormatTypeHtml        = 49  // Custom HTML format
	FormatTypeImage       = 50  // Custom image format
	FormatTypeOwnerLink   = 128 // CF_OWNERDISPLAY
	FormatTypeOemText     = 7   // CF_OEMTEXT
)

type Clip struct {
	ID             string    `gorm:"primaryKey;size:255" json:"id"`
	UserID         uint      `gorm:"index:idx_clips_user_group,priority:1;index:idx_clips_user_created,priority:1;index:idx_clips_user_updated,priority:1;not null" json:"user_id"`
	DeviceID       string    `gorm:"size:255;index" json:"device_id"`
	Description    string         `gorm:"type:text" json:"description"`
	Pinyin         string         `gorm:"type:text;index" json:"-"`
	CRC            int64     `gorm:"index:idx_clips_user_crc" json:"crc"`
	CreatedAt      time.Time `json:"created_at"`
	UpdatedAt      time.Time `gorm:"index:idx_clips_user_updated,priority:2" json:"updated_at"`
	DeletedAt      gorm.DeletedAt `gorm:"index" json:"-"` // Soft delete for sync
	GroupID        string    `gorm:"size:255;index:idx_clips_user_group,priority:2" json:"group_id"`
	ShortCut       int       `gorm:"default:0" json:"short_cut"`
	PasteCount     int       `gorm:"default:0" json:"paste_count"`
	IsConflictCopy bool      `gorm:"index:idx_clips_user_conflict;default:false" json:"is_conflict_copy"` // LWW losing clip kept for review
	WinClipID      string    `gorm:"size:255" json:"win_clip_id"` // ID of the winning clip this conflict copy belongs to
	DontSync       bool      `gorm:"default:false" json:"dont_sync"`
	ClipOrder      float64   `gorm:"default:0" json:"clip_order"`
	ClipGroupOrder float64   `gorm:"default:0" json:"clip_group_order"`

	Formats []ClipFormat `gorm:"foreignKey:ClipID;references:ID" json:"-"`

	User User `gorm:"foreignKey:UserID;references:ID" json:"-"`
	// DeviceID is a label for the source device. No FK constraint to allow
	// clips from devices that aren't registered in the devices table.
}

func (Clip) TableName() string {
	return "clips"
}
