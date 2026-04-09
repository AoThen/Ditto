package config

import (
	"os"
	"strconv"
	"strings"
	"time"
)

type Config struct {
	Port            string
	DatabasePath    string
	JWTSecret       string
	StartTime       time.Time
	CleanupInterval time.Duration // How often to run cleanup
	MaxClipAge      time.Duration // Maximum age of clips before removal
	MaxClipsPerUser int           // Maximum clips per user
	AllowedOrigins  []string      // CORS + WebSocket allowed origins
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

	jwtSecret := os.Getenv("JWT_SECRET")
	if jwtSecret == "" {
		jwtSecret = "change-me-in-production"
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

	// HIGH FIX (H4/H5): Read allowed origins from env, default to localhost dev origins.
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
	} else {
		// Default: localhost dev origins
		allowedOrigins = []string{
			"http://localhost:3000",
			"http://localhost:5173",
			"http://127.0.0.1:3000",
			"http://127.0.0.1:5173",
		}
	}

	return &Config{
		Port:            port,
		DatabasePath:    dbPath,
		JWTSecret:       jwtSecret,
		StartTime:       time.Now(),
		CleanupInterval: cleanupInterval,
		MaxClipAge:      maxClipAge,
		MaxClipsPerUser: maxClipsPerUser,
		AllowedOrigins:  allowedOrigins,
	}
}
