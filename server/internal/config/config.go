package config

import (
	"crypto/rand"
	"encoding/hex"
	"log"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"
)

// Token lifetime constants (single source of truth).
// Access tokens are short-lived so a stolen token has a small blast radius;
// the refresh token is what actually keeps a session alive across the 15
// minutes. Clients must implement the refresh flow, otherwise every 15 minutes
// they are logged out.
const (
	DefaultTokenExpiryAccess  = 15 * time.Minute
	DefaultTokenExpiryRefresh = 7 * 24 * time.Hour
)

type Config struct {
	Port               string
	DatabasePath       string
	JWTSecret          string
	StartTime          time.Time
	CleanupInterval    time.Duration // How often to run cleanup
	MaxClipAge         time.Duration // Maximum age of clips before removal
	MaxClipsPerUser    int           // Maximum clips per user
	SoftDeleteRetention time.Duration // How long to keep soft-deleted records before hard-delete
	AllowedOrigins     []string      // CORS + WebSocket allowed origins
	CookieSecure       bool          // Whether to set Secure flag on cookies
	TokenExpiryAccess  time.Duration
	TokenExpiryRefresh time.Duration
	MaxPushLimit       int           // Maximum clips per push sync request
	DefaultSyncPullLimit int         // Default pull limit per sync
	MaxSyncPullLimit   int           // Maximum pull limit per sync
	SlowThreshold      time.Duration // GORM slow query threshold
	StorageQuotaMB     int           // Per-user storage quota in MB (default 100)
}

// loadOrGenerateJWTSecret handles JWT secret loading with secure auto-generation
func loadOrGenerateJWTSecret() string {
	// Priority 1: Explicit environment variable
	if secret := os.Getenv("JWT_SECRET"); secret != "" {
		return secret
	}

	// Priority 2: Generate a cryptographically random secret and persist it
	dbPath := os.Getenv("DATABASE_PATH")
	if dbPath == "" {
		dbPath = "./data/ditto_cloud.db"
	}
	dataDir := filepath.Dir(dbPath)
	secretFile := filepath.Join(dataDir, ".jwt_secret")

	// Try loading existing persisted secret
	if data, err := os.ReadFile(secretFile); err == nil {
		s := strings.TrimSpace(string(data))
		if s != "" {
			return s
		}
	}

	// Generate a new 256-bit random secret
	secret := generateSecureSecret()

	// Persist it with restrictive permissions
	if err := os.MkdirAll(dataDir, 0755); err == nil {
		if err := os.WriteFile(secretFile, []byte(secret), 0600); err != nil {
			log.Printf("[WARN] Failed to persist JWT secret to %s: %v", secretFile, err)
		}
	}

	log.Printf("[INFO] Generated and persisted JWT secret to %s", secretFile)
	log.Printf("[INFO] For production, set JWT_SECRET env var instead")
	return secret
}

// generateSecureSecret generates a 256-bit cryptographically random secret
func generateSecureSecret() string {
	b := make([]byte, 32)
	if _, err := rand.Read(b); err != nil {
		log.Fatalf("[FATAL] Failed to generate secure random secret: %v", err)
	}
	return hex.EncodeToString(b)
}

func loadTokenExpiry(envVar string, defaultVal time.Duration) time.Duration {
	if env := os.Getenv(envVar); env != "" {
		if d, err := strconv.ParseInt(env, 10, 64); err == nil && d > 0 {
			return time.Duration(d) * time.Minute
		}
	}
	return defaultVal
}

func Load() *Config {
	port := os.Getenv("PORT")
	if port == "" {
		port = "8080"
	}

	dbPath := os.Getenv("DATABASE_PATH")
	if dbPath == "" {
		dbPath = "./data/ditto_cloud.db"
	}

	// C1 FIX: Auto-generate secure JWT secret if not set
	jwtSecret := loadOrGenerateJWTSecret()

	// A short secret can be brute-forced offline once an HMAC is captured, and
	// every token issued before a rotation becomes invalid. Refuse to start
	// instead of running in a knowingly weak configuration.
	if len(jwtSecret) < 32 {
		log.Fatalf("[FATAL] JWT_SECRET must be at least 32 bytes (got %d). Generate one with: openssl rand -hex 32", len(jwtSecret))
	}

	cleanupInterval := 24 * time.Hour // Default: daily
	if env := os.Getenv("CLEANUP_INTERVAL"); env != "" {
		if d, err := time.ParseDuration(env); err == nil {
			cleanupInterval = d
		}
	}

	maxClipAge := 30 * 24 * time.Hour // Default: 30 days
	if env := os.Getenv("MAX_CLIP_AGE"); env != "" {
		if d, err := time.ParseDuration(env); err == nil {
			maxClipAge = d
		}
	}

	maxClipsPerUser := 1000 // Default: 1000 clips
	if env := os.Getenv("MAX_CLIPS_PER_USER"); env != "" {
		if n, err := strconv.Atoi(env); err == nil && n > 0 {
			maxClipsPerUser = n
		}
	}

	// HIGH FIX (H4/H5): Read allowed origins from env. If empty, CORS is disabled
	// (recommended for same-origin deployment where Go serves both API and static files).
	allowedOriginsStr := os.Getenv("ALLOWED_ORIGINS")
	var allowedOrigins []string
	if allowedOriginsStr != "" {
		// Comma-separated list of origins
		for _, origin := range strings.Split(allowedOriginsStr, ",") {
			trimmed := strings.TrimSpace(origin)
			if trimmed != "" {
				allowedOrigins = append(allowedOrigins, trimmed)
			}
		}
	}
	// Default: empty = CORS disabled (same-origin deployment)

	// H1 FIX: Cookie Secure flag - default true for production, overridable for dev
	// Auto-detect localhost development: disable Secure flag when no TLS cert is configured
	cookieSecure := true
	if env := os.Getenv("INSECURE_COOKIES"); env == "true" || env == "1" {
		cookieSecure = false
		log.Printf("[INFO] INSECURE_COOKIES enabled: cookies will not require HTTPS")
	}
	// Auto-detect: if TLS cert is not configured, assume dev environment
	if os.Getenv("TLS_CERT") == "" {
		cookieSecure = false
		log.Printf("[INFO] No TLS cert configured: Secure cookie flag disabled for HTTP")
	}

	maxPushLimit := 1000
	if env := os.Getenv("MAX_PUSH_LIMIT"); env != "" {
		if n, err := strconv.Atoi(env); err == nil && n > 0 {
			maxPushLimit = n
		}
	}

	defaultSyncPullLimit := 1000
	if env := os.Getenv("SYNC_DEFAULT_PULL_LIMIT"); env != "" {
		if n, err := strconv.Atoi(env); err == nil && n > 0 {
			defaultSyncPullLimit = n
		}
	}

	maxSyncPullLimit := 5000
	if env := os.Getenv("SYNC_MAX_PULL_LIMIT"); env != "" {
		if n, err := strconv.Atoi(env); err == nil && n > 0 {
			maxSyncPullLimit = n
		}
	}

	slowThreshold := 500 * time.Millisecond
	if env := os.Getenv("DB_SLOW_THRESHOLD"); env != "" {
		if d, err := time.ParseDuration(env); err == nil {
			slowThreshold = d
		}
	}

	softDeleteRetention := 90 * 24 * time.Hour // Default: 90 days
	if env := os.Getenv("SOFT_DELETE_RETENTION"); env != "" {
		if d, err := time.ParseDuration(env); err == nil {
			softDeleteRetention = d
		}
	}

	storageQuotaMB := 100 // Default: 100MB
	if env := os.Getenv("STORAGE_QUOTA_MB"); env != "" {
		if n, err := strconv.Atoi(env); err == nil && n > 0 {
			storageQuotaMB = n
		}
	}

	return &Config{
		Port:               port,
		DatabasePath:       dbPath,
		JWTSecret:          jwtSecret,
		StartTime:          time.Now(),
		CleanupInterval:    cleanupInterval,
		MaxClipAge:         maxClipAge,
		MaxClipsPerUser:    maxClipsPerUser,
		AllowedOrigins:     allowedOrigins,
		CookieSecure:       cookieSecure,
		TokenExpiryAccess:  loadTokenExpiry("JWT_ACCESS_TOKEN_EXPIRY", DefaultTokenExpiryAccess),
		TokenExpiryRefresh: loadTokenExpiry("JWT_REFRESH_TOKEN_EXPIRY", DefaultTokenExpiryRefresh),
		MaxPushLimit:        maxPushLimit,
		DefaultSyncPullLimit:  defaultSyncPullLimit,
		MaxSyncPullLimit:      maxSyncPullLimit,
		SlowThreshold:         slowThreshold,
		SoftDeleteRetention:   softDeleteRetention,
		StorageQuotaMB:        storageQuotaMB,
	}
}
