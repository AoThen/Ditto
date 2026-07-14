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
	groupSvc := service.NewGroupService()
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
	groupHandler := handler.NewGroupHandler(groupSvc)
	wsHandler := handler.NewWSHandler(h, cfg)
	statsHandler := handler.NewStatsHandler()

	// HIGH FIX (H5): Configure WebSocket allowed origins
	handler.SetAllowedOrigins(cfg.AllowedOrigins)

	// Setup Gin
	gin.SetMode(gin.ReleaseMode)
	r := gin.New()
	r.Use(gin.Recovery())

	// CORS middleware (restrict to configured origins - HIGH FIX H4)
	r.Use(cors.New(cors.Config{
		AllowOrigins:     cfg.AllowedOrigins,
		AllowMethods:     []string{"GET", "POST", "PUT", "DELETE", "OPTIONS"},
		AllowHeaders:     []string{"Origin", "Content-Type", "Authorization", "Sec-WebSocket-Protocol"},
		ExposeHeaders:    []string{"Content-Length", "Content-Disposition"},
		AllowCredentials: true,
	}))

	// Health check with stats
	r.GET("/health", func(c *gin.Context) {
		var userCount, clipCount int64
		database.DB.Model(&model.User{}).Count(&userCount)
		database.DB.Model(&model.Clip{}).Count(&clipCount)

		c.JSON(200, gin.H{
			"status":      "ok",
			"total_users": userCount,
			"total_clips": clipCount,
			"uptime":      time.Since(cfg.StartTime).Round(time.Second).String(),
		})
	})

	// Public routes
	v1 := r.Group("/api/v1")
	{
		auth := v1.Group("/auth")
		{
			// C4 FIX: Rate limit registration to prevent abuse (uses IP-based tracking)
			auth.POST("/register", rateLimiter.LoginRateLimit(), authHandler.Register)
			auth.POST("/login", rateLimiter.LoginRateLimit(), authHandler.Login)
		}
	}

	// Semi-protected routes (need valid token but not device-specific)
	semiProtected := v1.Group("")
	semiProtected.Use(middleware.Auth(cfg))
	{
		auth := semiProtected.Group("/auth")
		{
			auth.POST("/refresh", authHandler.Refresh)
			auth.POST("/logout", authHandler.Logout)
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
			clips.GET("/changes", clipHandler.GetChanges)
			clips.GET("/conflicts", clipHandler.ListConflictClips)
			clips.GET("/:id", clipHandler.GetClip)
			clips.GET("/:id/download", clipHandler.DownloadClip)
			clips.DELETE("/:id", clipHandler.DeleteClip)
			clips.POST("/sync", clipHandler.Sync)
			clips.POST("/conflicts/:id/resolve", clipHandler.ResolveConflictClip)
			clips.POST("/remove-from-group", groupHandler.RemoveClipsFromGroup)
		}

		groups := protected.Group("/groups")
		{
			groups.GET("", groupHandler.ListGroups)
			groups.GET("/:id", groupHandler.GetGroup)
			groups.POST("", groupHandler.CreateGroup)
			groups.PUT("/:id", groupHandler.UpdateGroup)
			groups.DELETE("/:id", groupHandler.DeleteGroup)
			groups.POST("/:id/move-clips", groupHandler.MoveClipsToGroup)
		}

		encryption := protected.Group("/encryption")
		{
			encryption.POST("/setup", encryptionHandler.SetupEncryption)
			encryption.GET("/salt", encryptionHandler.GetEncryptionSalt)
			encryption.GET("/key-material", encryptionHandler.GetKeyMaterial)
			encryption.POST("/disable", encryptionHandler.DisableEncryption)
			encryption.POST("/change-password", encryptionHandler.ChangeEncryptionPassword)
		}

		stats := protected.Group("/stats")
		{
			stats.GET("/overview", statsHandler.GetOverview)
			stats.GET("/sync-logs", statsHandler.GetSyncLogs)
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
