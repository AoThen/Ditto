package main

import (
	"context"
	"log"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"ditto-cloud-server/internal/config"
	"ditto-cloud-server/internal/database"
	"ditto-cloud-server/internal/handler"
	"ditto-cloud-server/internal/hub"
	"ditto-cloud-server/internal/middleware"
	"ditto-cloud-server/internal/model"
	"ditto-cloud-server/internal/service"

	cors "github.com/gin-contrib/cors"
	"github.com/gin-gonic/gin"
	"github.com/joho/godotenv"
)

func main() {
	// Load .env file if it exists
	_ = godotenv.Load()

	cfg := config.Load()

	// Check for TLS configuration
	tlsCert := os.Getenv("TLS_CERT")
	tlsKey := os.Getenv("TLS_KEY")
	useTLS := tlsCert != "" && tlsKey != ""

	// Initialize database
	if err := database.Init(cfg.DatabasePath); err != nil {
		log.Fatalf("Failed to initialize database: %v", err)
	}

	// Initialize WebSocket hub
	h := hub.New()
	h.Run()

	// Initialize services
	authSvc := service.NewAuthService(cfg)
	deviceSvc := service.NewDeviceService()
	clipSvc := service.NewClipService(h)
	encryptionSvc := service.NewEncryptionService()
	rateLimiter := middleware.NewRateLimiter()
	cleanupSvc := service.NewCleanupService(cfg)

	// Start cleanup service (background goroutine)
	cleanupStop := make(chan struct{})
	go cleanupSvc.Start(cleanupStop)

	// Initialize handlers
	authHandler := handler.NewAuthHandler(authSvc, rateLimiter)
	deviceHandler := handler.NewDeviceHandler(deviceSvc)
	clipHandler := handler.NewClipHandler(clipSvc)
	encryptionHandler := handler.NewEncryptionHandler(encryptionSvc)
	wsHandler := handler.NewWSHandler(h, cfg)

	// Setup Gin
	gin.SetMode(gin.ReleaseMode)
	r := gin.New()
	r.Use(gin.Recovery())

	// CORS middleware (allow all origins for development)
	r.Use(cors.Default())

	// Health check with stats
	r.GET("/health", func(c *gin.Context) {
		var userCount, clipCount int64
		database.DB.Model(&model.User{}).Count(&userCount)
		database.DB.Model(&model.Clip{}).Count(&clipCount)

		c.JSON(200, gin.H{
			"status":       "ok",
			"total_users":  userCount,
			"total_clips":  clipCount,
			"uptime":       time.Since(cfg.StartTime).Round(time.Second).String(),
		})
	})

	// Public routes
	v1 := r.Group("/api/v1")
	{
		auth := v1.Group("/auth")
		{
			auth.POST("/register", authHandler.Register)
			auth.POST("/login", rateLimiter.LoginRateLimit(), authHandler.Login)
		}
	}

	// Protected routes
	protected := v1.Group("")
	protected.Use(middleware.Auth(cfg))
	{
		devices := protected.Group("/devices")
		{
			devices.GET("", deviceHandler.ListDevices)
			devices.DELETE("/:id", deviceHandler.RemoveDevice)
		}

		clips := protected.Group("/clips")
		{
			clips.GET("", clipHandler.ListClips)
			clips.GET("/:id", clipHandler.GetClip)
			clips.DELETE("/:id", clipHandler.DeleteClip)
			clips.POST("/sync", clipHandler.Sync)
		}

		encryption := protected.Group("/encryption")
		{
			encryption.POST("/setup", encryptionHandler.SetupEncryption)
			encryption.GET("/salt", encryptionHandler.GetEncryptionSalt)
		}

		// WebSocket route (uses query param auth since headers can't be set during WS upgrade)
		protected.GET("/ws", wsHandler.HandleWebSocket)
	}

	// Create HTTP server
	srv := &http.Server{
		Addr:    ":" + cfg.Port,
		Handler: r,
	}

	// Graceful shutdown
	go func() {
		quit := make(chan os.Signal, 1)
		signal.Notify(quit, syscall.SIGINT, syscall.SIGTERM)
		<-quit
		log.Println("Shutting down server...")

		// Stop cleanup service
		close(cleanupStop)

		// Shutdown WebSocket hub (closes all connections gracefully)
		h.Shutdown()

		// Shutdown HTTP server with timeout
		ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
		defer cancel()
		if err := srv.Shutdown(ctx); err != nil {
			log.Fatalf("Server forced to shutdown: %v", err)
		}
	}()

	log.Printf("Starting Ditto Cloud server on :%s (TLS: %v)", cfg.Port, useTLS)
	var err error
	if useTLS {
		log.Printf("TLS enabled: cert=%s key=%s", tlsCert, tlsKey)
		err = srv.ListenAndServeTLS(tlsCert, tlsKey)
	} else {
		err = srv.ListenAndServe()
	}
	if err != nil && err != http.ErrServerClosed {
		log.Fatalf("Failed to start server: %v", err)
	}

	log.Println("Server exited gracefully")
}
