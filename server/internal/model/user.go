package model

import "time"

type User struct {
	ID           uint       `gorm:"primaryKey;autoIncrement" json:"id"`
	Username     string     `gorm:"uniqueIndex;size:255;not null" json:"username"`
	Email        string     `gorm:"uniqueIndex;size:255;not null" json:"email"`
	PasswordHash string     `gorm:"column:password_hash;size:255;not null" json:"-"`
	CreatedAt    time.Time  `json:"created_at"`
	UpdatedAt    time.Time  `json:"updated_at"`
}

func (User) TableName() string {
	return "users"
}
