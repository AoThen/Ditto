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

// Token lifetime constants (single source of truth)
const (
	DefaultTokenExpiryAccess  = 30 * 24 * time.Hour // 30 days
	DefaultTokenExpiryRefresh = 90 * 24 * time.Hour // 90 days
)

type Config struct {
	Port               string
	DatabasePath       string
	JWTSecret          string
	StartTime          time.Time
	CleanupInterval    time.Duration // How often to run cleanup
	MaxClipAge         time.Duration // Maximum age of clips before removal
	MaxClipsPerUser    int           // Maximum clips per user
	AllowedOrigins     []string      // CORS + WebSocket allowed origins
	CookieSecure       bool          // Whether to set Secure flag on cookies
	TokenExpiryAccess  time.Duration
	TokenExpiryRefresh time.Duration
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

	// Warn about weak secrets (only when explicitly set to a short/predictable value)
	if len(jwtSecret) < 32 {
		log.Printf("[WARNING] JWT_SECRET is shorter than 32 bytes. Use a strong random secret in production.")
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
	}
}
