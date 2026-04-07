package model

import "time"

type ClipFormat struct {
	ID         uint      `gorm:"primaryKey;autoIncrement" json:"id"`
	ClipID     string    `gorm:"size:255;index:idx_clip_formats_clip;not null" json:"clip_id"`
	FormatType int       `gorm:"not null" json:"format_type"`
	Data       []byte    `json:"data"`
	CreatedAt  time.Time `json:"created_at"`

	Clip Clip `gorm:"foreignKey:ClipID;references:ID;constraint:OnDelete:CASCADE" json:"-"`
}

func (ClipFormat) TableName() string {
	return "clip_formats"
}
