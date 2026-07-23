package model

import "time"

type RateLimitRecord struct {
	Key       string     `gorm:"primaryKey;size:255" json:"key"`
	FailCount int        `gorm:"default:0" json:"fail_count"`
	BanUntil  *time.Time `json:"ban_until"`
	UpdatedAt time.Time  `json:"updated_at"`
}

func (RateLimitRecord) TableName() string {
	return "rate_limit_records"
}
