package database

import (
	"fmt"
	"log"
	"os"
	"path/filepath"
	"strings"
	"time"

	"ditto-cloud-server/internal/model"
	"ditto-cloud-server/internal/utils"

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
		Logger: logger.New(
			log.New(os.Stdout, "\r\n", log.LstdFlags),
			logger.Config{
				SlowThreshold:             500 * time.Millisecond,
				LogLevel:                  logLevel,
				IgnoreRecordNotFoundError: true,
				Colorful:                  false,
				ParameterizedQueries:      true,
			},
		),
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

	BackfillPinyin()

	return nil
}

// BackfillPinyin populates the pinyin column for existing clips that have empty pinyin.
func BackfillPinyin() {
	var count int64
	DB.Model(&model.Clip{}).Where("pinyin IS NULL OR pinyin = ''").Count(&count)
	if count == 0 {
		return
	}
	log.Printf("[Backfill] Backfilling pinyin for %d existing clips...", count)

	const batchSize = 100
	var clips []model.Clip
	DB.Model(&model.Clip{}).Where("pinyin IS NULL OR pinyin = ''").FindInBatches(&clips, batchSize, func(tx *gorm.DB, batch int) error {
		for _, clip := range clips {
			pinyin := utils.ConvertToPinyin(clip.Description)
			tx.Model(&clip).Update("pinyin", pinyin)
		}
		return nil
	})
	log.Printf("[Backfill] Pinyin backfill completed")
}
