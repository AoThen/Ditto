package model

import "time"

type ClipFormat struct {
	ID         uint      `gorm:"primaryKey;autoIncrement" json:"id"`
	ClipID     string    `gorm:"size:255;index:idx_clip_formats_clip_fmt,priority:1;not null" json:"clip_id"`
	FormatType int       `gorm:"not null;index:idx_clip_formats_clip_fmt,priority:2" json:"format_type"`
	Data       []byte    `json:"data"`
	Encrypted  bool      `gorm:"default:false" json:"encrypted"` // true if data is E2E encrypted
	CreatedAt  time.Time `json:"created_at"`

	Clip Clip `gorm:"foreignKey:ClipID;references:ID;constraint:OnDelete:CASCADE" json:"-"`
}

func (ClipFormat) TableName() string {
	return "clip_formats"
}
