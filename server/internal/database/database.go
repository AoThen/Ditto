package database

import (
	"fmt"
	"log"
	"os"
	"path/filepath"
	"strings"

	"ditto-cloud-server/internal/model"

	"gorm.io/driver/sqlite"
	"gorm.io/gorm"
	"gorm.io/gorm/logger"
)

var DB *gorm.DB

func Init(dbPath string) error {
	// Ensure data directory exists
	dir := filepath.Dir(dbPath)
	if err := os.MkdirAll(dir, 0755); err != nil {
		return fmt.Errorf("failed to create data directory: %w", err)
	}

	// Open with WAL mode and foreign keys enabled
	dsn := fmt.Sprintf("file:%s?_busy_timeout=5000&_fk=true&_journal_mode=WAL", dbPath)

	var err error

	logLevel := logger.Warn
	switch strings.ToLower(os.Getenv("GORM_LOG_LEVEL")) {
	case "info":
		logLevel = logger.Info
	case "warn":
		logLevel = logger.Warn
	case "error":
		logLevel = logger.Error
	case "silent":
		logLevel = logger.Silent
	default:
		if v := os.Getenv("GORM_LOG_LEVEL"); v != "" {
			log.Printf("[Database] Unknown GORM_LOG_LEVEL=%q, using default warn", v)
		}
	}
	log.Printf("[Database] GORM log level set")

	DB, err = gorm.Open(sqlite.Open(dsn), &gorm.Config{
		Logger: logger.Default.LogMode(logLevel),
	})
	if err != nil {
		return fmt.Errorf("failed to connect database: %w", err)
	}

	// Auto-migrate all tables
	if err := DB.AutoMigrate(
		&model.User{},
		&model.Device{},
		&model.Clip{},
		&model.ClipFormat{},
		&model.Group{},
		&model.SyncLog{},
		&model.RateLimitRecord{},
		&model.EncryptionSettings{},
	); err != nil {
		return fmt.Errorf("failed to migrate database: %w", err)
	}

	return nil
}
