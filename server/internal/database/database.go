package database

import (
	"database/sql"
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
	"gorm.io/gorm/clause"
	"gorm.io/gorm/logger"
)

var DB *gorm.DB

// DataStore 定义了数据库操作接口，便于测试 mock
type DataStore interface {
	Create(value interface{}) *gorm.DB
	First(dest interface{}, conds ...interface{}) *gorm.DB
	Last(dest interface{}, conds ...interface{}) *gorm.DB
	Where(query interface{}, args ...interface{}) *gorm.DB
	Model(value interface{}) *gorm.DB
	Select(query interface{}, args ...interface{}) *gorm.DB
	Joins(query string, args ...interface{}) *gorm.DB
	Raw(sql string, values ...interface{}) *gorm.DB
	Exec(sql string, values ...interface{}) *gorm.DB
	Save(value interface{}) *gorm.DB
	Delete(value interface{}, conds ...interface{}) *gorm.DB
	Transaction(fc func(tx *gorm.DB) error, opts ...*sql.TxOptions) error
	Table(name string, args ...interface{}) *gorm.DB
	Order(value interface{}) *gorm.DB
	Limit(limit int) *gorm.DB
	Offset(offset int) *gorm.DB
	Count(count *int64) *gorm.DB
	Find(dest interface{}, conds ...interface{}) *gorm.DB
	Pluck(column string, value interface{}) *gorm.DB
	Scopes(funcs ...func(*gorm.DB) *gorm.DB) *gorm.DB
	Preload(query string, args ...interface{}) *gorm.DB
	Clauses(conds ...clause.Expression) *gorm.DB
	Update(column string, value interface{}) *gorm.DB
	Updates(values interface{}) *gorm.DB
	Session(config *gorm.Session) *gorm.DB
	Unscoped() *gorm.DB
	Debug() *gorm.DB
	Distinct(args ...interface{}) *gorm.DB
	Group(name string) *gorm.DB
	Having(query interface{}, args ...interface{}) *gorm.DB
	Scan(dest interface{}) *gorm.DB
}

// SetDB 允许在测试中注入 mock 数据库
func SetDB(db *gorm.DB) {
	DB = db
}

func Init(dbPath string, slowThreshold time.Duration) error {
	driver := os.Getenv("DB_DRIVER")
	if driver == "" {
		driver = "sqlite"
	}
	switch driver {
	case "postgres", "postgresql":
		return InitPostgres(dbPath, slowThreshold)
	default:
		return InitSQLite(dbPath, slowThreshold)
	}
}

func InitSQLite(dbPath string, slowThreshold time.Duration) error {
	// Ensure data directory exists
	dir := filepath.Dir(dbPath)
	if err := os.MkdirAll(dir, 0755); err != nil {
		return fmt.Errorf("failed to create data directory: %w", err)
	}

	// Open with WAL mode and foreign keys enabled
	dsn := fmt.Sprintf("file:%s?_busy_timeout=5000&_fk=true&_journal_mode=WAL&_txlock=immediate", dbPath)

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
				SlowThreshold:             slowThreshold,
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
		&model.Migration{},
	); err != nil {
		return fmt.Errorf("failed to migrate database: %w", err)
	}

	if err := recordMigrationVersion(DB); err != nil {
		return fmt.Errorf("failed to record migration version: %w", err)
	}

	BackfillPinyin()

	return nil
}

func InitPostgres(dsn string, slowThreshold time.Duration) error {
	return fmt.Errorf("PostgreSQL 驱动需要在 go.mod 中添加 gorm.io/driver/postgres")
}

func recordMigrationVersion(db *gorm.DB) error {
	if db.Migrator().HasTable(&model.Migration{}) {
		var count int64
		db.Model(&model.Migration{}).Count(&count)
		if count == 0 {
			return db.Create(&model.Migration{Version: "000001_init", AppliedAt: time.Now()}).Error
		}
	}
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
