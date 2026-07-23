package model

import "time"

type Group struct {
	ID          string    `gorm:"primaryKey;size:255" json:"id"`
	UserID      uint      `gorm:"index:idx_groups_user;not null" json:"user_id"`
	Name        string    `gorm:"type:text;not null" json:"name"`
	Description string    `gorm:"type:text" json:"description"`
	ParentID    *string   `gorm:"size:255;index" json:"parent_id"`
	ClipOrder   float64   `gorm:"default:0" json:"clip_order"`
	CreatedAt   time.Time `json:"created_at"`
	UpdatedAt   time.Time `json:"updated_at"`

	User User `gorm:"foreignKey:UserID;references:ID" json:"-"`
}

func (Group) TableName() string {
	return "groups"
}
