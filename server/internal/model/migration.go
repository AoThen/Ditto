package model

import "time"

type Migration struct {
	ID        uint      `gorm:"primaryKey;autoIncrement" json:"id"`
	Version   string    `gorm:"uniqueIndex;size:50;not null" json:"version"`
	AppliedAt time.Time `gorm:"autoCreateTime" json:"applied_at"`
}

func (Migration) TableName() string {
	return "schema_migrations"
}