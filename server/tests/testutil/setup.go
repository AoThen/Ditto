package testutil

import (
	"bytes"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"strings"
	"testing"
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
	"github.com/golang-jwt/jwt/v5"
	"gorm.io/gorm"
)

// TestDB exposes the current test database for direct queries.
var TestDB *gorm.DB

// SetupTestServer creates a fresh Gin server with an isolated test database.
// Returns the httptest.Server, *config.Config, and the database path for cleanup.
func SetupTestServer(t *testing.T) (*httptest.Server, *config.Config) {
	t.Helper()
	gin.SetMode(gin.TestMode)

	// Use a unique temp file for each test
	tmpFile, err := os.CreateTemp("", "ditto_test_*.db")
	if err != nil {
		t.Fatalf("failed to create temp db file: %v", err)
	}
	dbPath := tmpFile.Name()
	tmpFile.Close()

	cfg := &config.Config{
		Port:               "0",
		DatabasePath:       dbPath,
		JWTSecret:          "test-secret-key-for-testing",
		StartTime:          time.Now(),
		CookieSecure:       false, // tests run over HTTP, not HTTPS
		TokenExpiryAccess:  config.DefaultTokenExpiryAccess,
		TokenExpiryRefresh: config.DefaultTokenExpiryRefresh,
	}

	if err := database.Init(dbPath, 500*time.Millisecond); err != nil {
		t.Fatalf("failed to init database: %v", err)
	}
	TestDB = database.DB

	authSvc := service.NewAuthService(cfg)
	deviceSvc := service.NewDeviceService()
	clipSvc := service.NewClipService(nil, 1000, 1000, 5000, 100) // nil broadcaster for tests (no WS needed)
	encryptionSvc := service.NewEncryptionService()
	groupSvc := service.NewGroupService()
	statsSvc := service.NewStatsService()
	rateLimiter := middleware.NewRateLimiter()

	authHandler := handler.NewAuthHandler(authSvc, rateLimiter)
	deviceHandler := handler.NewDeviceHandler(deviceSvc)
	clipHandler := handler.NewClipHandler(clipSvc)
	encryptionHandler := handler.NewEncryptionHandler(encryptionSvc)
	groupHandler := handler.NewGroupHandler(groupSvc)
	statsHandler := handler.NewStatsHandler(statsSvc)
	userSvc := service.NewUserService()
	adminHandler := handler.NewAdminHandler(userSvc)

	r := gin.New()
	r.Use(gin.Recovery())
	r.Use(cors.Default())

	// Health check
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
			auth.POST("/register", authHandler.Register)
			auth.POST("/login", rateLimiter.LoginRateLimit(), authHandler.Login)
		}
	}

	// Semi-protected routes (need valid token but not device-specific)
	semiProtected := v1.Group("")
	semiProtected.Use(middleware.Auth(cfg))
	{
		semiAuth := semiProtected.Group("/auth")
		{
			semiAuth.POST("/refresh", authHandler.Refresh)
		}
	}

	// Protected routes
	protected := v1.Group("")
	protected.Use(middleware.Auth(cfg))
	{
		protected.POST("/auth/logout", authHandler.Logout)

		devices := protected.Group("/devices")
		{
			devices.GET("", deviceHandler.ListDevices)
			devices.DELETE("/:id", deviceHandler.RemoveDevice)
			devices.PATCH("/:id", deviceHandler.UpdateDevice)
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
			clips.POST("/batch-delete", clipHandler.BatchDeleteClips)
			clips.POST("/batch-dont-sync", clipHandler.BatchMarkDontSync)
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

		// Admin routes
		admin := protected.Group("/admin")
		admin.Use(middleware.AdminAuth())
		{
			admin.POST("/users", adminHandler.CreateUser)
			admin.GET("/users", adminHandler.ListUsers)
			admin.GET("/users/:id", adminHandler.GetUser)
			admin.PUT("/users/:id", adminHandler.UpdateUser)
			admin.DELETE("/users/:id", adminHandler.DeleteUser)
			admin.POST("/users/:id/reset-password", adminHandler.ResetPassword)
		}
	}

	server := httptest.NewServer(r)

	t.Cleanup(func() {
		server.Close()
		database.DB = nil
		os.Remove(dbPath)
		os.Remove(dbPath + "-shm")
		os.Remove(dbPath + "-wal")
	})

	return server, cfg
}

// SetupTestServerWithShortToken creates a test server with a very short JWT token expiry (1 second).
// Used for testing token expiration scenarios.
func SetupTestServerWithShortToken(t *testing.T) (*httptest.Server, *config.Config) {
	t.Helper()
	gin.SetMode(gin.TestMode)

	tmpFile, err := os.CreateTemp("", "ditto_test_*.db")
	if err != nil {
		t.Fatalf("failed to create temp db file: %v", err)
	}
	dbPath := tmpFile.Name()
	tmpFile.Close()

	cfg := &config.Config{
		Port:               "0",
		DatabasePath:       dbPath,
		JWTSecret:          "test-secret-key-for-testing",
		StartTime:          time.Now(),
		CookieSecure:       false,
		TokenExpiryAccess:  2 * time.Second, // short for testing
		TokenExpiryRefresh: config.DefaultTokenExpiryRefresh,
	}

	if err := database.Init(dbPath, 500*time.Millisecond); err != nil {
		t.Fatalf("failed to init database: %v", err)
	}
	TestDB = database.DB

	authSvc := service.NewAuthService(cfg)
	deviceSvc := service.NewDeviceService()
	clipSvc := service.NewClipService(nil, 1000, 1000, 5000, 100) // nil broadcaster for tests (no WS needed)
	encryptionSvc := service.NewEncryptionService()
	groupSvc := service.NewGroupService()
	statsSvc := service.NewStatsService()
	rateLimiter := middleware.NewRateLimiter()

	authHandler := handler.NewAuthHandler(authSvc, rateLimiter)
	deviceHandler := handler.NewDeviceHandler(deviceSvc)
	clipHandler := handler.NewClipHandler(clipSvc)
	encryptionHandler := handler.NewEncryptionHandler(encryptionSvc)
	groupHandler := handler.NewGroupHandler(groupSvc)
	statsHandler := handler.NewStatsHandler(statsSvc)
	userSvc := service.NewUserService()
	adminHandler := handler.NewAdminHandler(userSvc)

	r := gin.New()
	r.Use(gin.Recovery())
	r.Use(cors.Default())

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

	v1 := r.Group("/api/v1")
	{
		auth := v1.Group("/auth")
		{
			auth.POST("/register", authHandler.Register)
			auth.POST("/login", rateLimiter.LoginRateLimit(), authHandler.Login)
		}
	}

	semiProtected := v1.Group("")
	semiProtected.Use(middleware.Auth(cfg))
	{
		semiProtected.POST("/auth/refresh", authHandler.Refresh)
	}

	protected := v1.Group("")
	protected.Use(middleware.Auth(cfg))
	{
		protected.POST("/auth/logout", authHandler.Logout)

		devices := protected.Group("/devices")
		{
			devices.GET("", deviceHandler.ListDevices)
			devices.DELETE("/:id", deviceHandler.RemoveDevice)
			devices.PATCH("/:id", deviceHandler.UpdateDevice)
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
			clips.POST("/batch-delete", clipHandler.BatchDeleteClips)
			clips.POST("/batch-dont-sync", clipHandler.BatchMarkDontSync)
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

		admin := protected.Group("/admin")
		admin.Use(middleware.AdminAuth())
		{
			admin.POST("/users", adminHandler.CreateUser)
			admin.GET("/users", adminHandler.ListUsers)
			admin.GET("/users/:id", adminHandler.GetUser)
			admin.PUT("/users/:id", adminHandler.UpdateUser)
			admin.DELETE("/users/:id", adminHandler.DeleteUser)
			admin.POST("/users/:id/reset-password", adminHandler.ResetPassword)
		}
	}

	server := httptest.NewServer(r)

	t.Cleanup(func() {
		server.Close()
		database.DB = nil
		os.Remove(dbPath)
		os.Remove(dbPath + "-shm")
		os.Remove(dbPath + "-wal")
	})

	return server, cfg
}

// SetupTestServerWithWS creates a test server with WebSocket support.
// Returns the httptest.Server, *config.Config, and the WebSocket Hub.
// The caller is responsible for shutting down the Hub via t.Cleanup.
func SetupTestServerWithWS(t *testing.T) (*httptest.Server, *config.Config, *hub.Hub) {
	t.Helper()
	gin.SetMode(gin.TestMode)

	tmpFile, err := os.CreateTemp("", "ditto_test_*.db")
	if err != nil {
		t.Fatalf("failed to create temp db file: %v", err)
	}
	dbPath := tmpFile.Name()
	tmpFile.Close()

	cfg := &config.Config{
		Port:               "0",
		DatabasePath:       dbPath,
		JWTSecret:          "test-secret-key-for-testing",
		StartTime:          time.Now(),
		CookieSecure:       false,
		TokenExpiryAccess:  config.DefaultTokenExpiryAccess,
		TokenExpiryRefresh: config.DefaultTokenExpiryRefresh,
	}

	if err := database.Init(dbPath, 500*time.Millisecond); err != nil {
		t.Fatalf("failed to init database: %v", err)
	}
	TestDB = database.DB

	// Initialize WebSocket hub
	h := hub.New()
	h.Run()

	authSvc := service.NewAuthService(cfg)
	deviceSvc := service.NewDeviceService()
	clipSvc := service.NewClipService(h, 1000, 1000, 5000, 100) // hub as broadcaster for real-time push
	encryptionSvc := service.NewEncryptionService()
	groupSvc := service.NewGroupService()
	statsSvc := service.NewStatsService()
	rateLimiter := middleware.NewRateLimiter()

	authHandler := handler.NewAuthHandler(authSvc, rateLimiter)
	deviceHandler := handler.NewDeviceHandler(deviceSvc)
	clipHandler := handler.NewClipHandler(clipSvc)
	encryptionHandler := handler.NewEncryptionHandler(encryptionSvc)
	groupHandler := handler.NewGroupHandler(groupSvc)
	statsHandler := handler.NewStatsHandler(statsSvc)
	wsHandler := handler.NewWSHandler(h, cfg)
	userSvc := service.NewUserService()
	adminHandler := handler.NewAdminHandler(userSvc)

	r := gin.New()
	r.Use(gin.Recovery())
	r.Use(cors.Default())

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

	v1 := r.Group("/api/v1")
	{
		auth := v1.Group("/auth")
		{
			auth.POST("/register", authHandler.Register)
			auth.POST("/login", rateLimiter.LoginRateLimit(), authHandler.Login)
		}
	}

	semiProtected := v1.Group("")
	semiProtected.Use(middleware.Auth(cfg))
	{
		semiProtected.POST("/auth/refresh", authHandler.Refresh)
	}

	protected := v1.Group("")
	protected.Use(middleware.Auth(cfg))
	{
		protected.POST("/auth/logout", authHandler.Logout)

		devices := protected.Group("/devices")
		{
			devices.GET("", deviceHandler.ListDevices)
			devices.DELETE("/:id", deviceHandler.RemoveDevice)
			devices.PATCH("/:id", deviceHandler.UpdateDevice)
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
			clips.POST("/batch-delete", clipHandler.BatchDeleteClips)
			clips.POST("/batch-dont-sync", clipHandler.BatchMarkDontSync)
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

		// WebSocket route
		protected.GET("/ws", wsHandler.HandleWebSocket)

		// Admin routes
		admin := protected.Group("/admin")
		admin.Use(middleware.AdminAuth())
		{
			admin.POST("/users", adminHandler.CreateUser)
			admin.GET("/users", adminHandler.ListUsers)
			admin.GET("/users/:id", adminHandler.GetUser)
			admin.PUT("/users/:id", adminHandler.UpdateUser)
			admin.DELETE("/users/:id", adminHandler.DeleteUser)
			admin.POST("/users/:id/reset-password", adminHandler.ResetPassword)
		}
	}

	server := httptest.NewServer(r)

	t.Cleanup(func() {
		server.Close()
		h.Shutdown()
		database.DB = nil
		os.Remove(dbPath)
		os.Remove(dbPath + "-shm")
		os.Remove(dbPath + "-wal")
	})

	return server, cfg, h
}

// RegisterUser registers a user directly via HTTP and returns status code and raw response body.
func RegisterUser(t *testing.T, server *httptest.Server, username, email, password string) (int, []byte) {
	t.Helper()
	body := map[string]string{
		"username": username,
		"email":    email,
		"password": password,
	}
	return doJSON(t, server, "POST", "/api/v1/auth/register", "", body, "")
}

// LoginUser logs in a user directly via HTTP and returns status code and raw response body.
func LoginUser(t *testing.T, server *httptest.Server, username, password string) (int, []byte) {
	t.Helper()
	body := map[string]string{
		"username": username,
		"password": password,
	}
	statusCode, respBody, _ := doJSONWithCookies(t, server, "POST", "/api/v1/auth/login", "", body, "")
	return statusCode, respBody
}

// LoginUserWithCookies logs in a user and returns Set-Cookie headers.
// H1: Used to extract HttpOnly tokens.
func LoginUserWithCookies(t *testing.T, server *httptest.Server, username, password string) (int, []byte, []string) {
	t.Helper()
	body := map[string]string{
		"username": username,
		"password": password,
	}
	return doJSONWithCookies(t, server, "POST", "/api/v1/auth/login", "", body, "")
}

// RegisterAndLogin is a convenience function that registers then logs in a user.
// Returns the auth token and device_id.
// H1: Token is now in HttpOnly cookie, extracted from Set-Cookie header.
func RegisterAndLogin(t *testing.T, server *httptest.Server, username, email, password string) (token, deviceID string) {
	t.Helper()
	statusCode, _ := RegisterUser(t, server, username, email, password)
	if statusCode != http.StatusOK {
		t.Fatalf("expected register status 200, got %d", statusCode)
	}

	statusCode, loginRespBody, setCookies := LoginUserWithCookies(t, server, username, password)
	if statusCode != http.StatusOK {
		t.Fatalf("expected login status 200, got %d", statusCode)
	}

	// H1: Extract device_token from Set-Cookie header
	token = ExtractCookie(setCookies, "device_token")
	if token == "" {
		t.Fatalf("login response missing device_token cookie. Set-Cookie headers: %v", setCookies)
	}

	_, _, data := ParseResponse(t, loginRespBody)
	deviceID, _ = data["device_id"].(string)

	return token, deviceID
}

// ExtractCookie extracts a cookie value from Set-Cookie headers.
// H1: Used to extract HttpOnly tokens from login responses.
func ExtractCookie(setCookies []string, name string) string {
	for _, cookie := range setCookies {
		if strings.Contains(cookie, name+"=") {
			parts := strings.SplitN(cookie, "=", 2)
			if len(parts) == 2 {
				return strings.SplitN(parts[1], ";", 2)[0]
			}
		}
	}
	return ""
}

// AuthGet performs an authenticated GET request.
func AuthGet(t *testing.T, server *httptest.Server, path, token string) (int, []byte) {
	t.Helper()
	return doJSON(t, server, "GET", path, token, nil, "")
}

// AuthPost performs an authenticated POST request.
func AuthPost(t *testing.T, server *httptest.Server, path, token string, body interface{}) (int, []byte) {
	t.Helper()
	return doJSON(t, server, "POST", path, token, body, "")
}

// AuthDelete performs an authenticated DELETE request.
func AuthDelete(t *testing.T, server *httptest.Server, path, token string) (int, []byte) {
	t.Helper()
	return doJSON(t, server, "DELETE", path, token, nil, "")
}

// AuthPut performs an authenticated PUT request.
func AuthPut(t *testing.T, server *httptest.Server, path, token string, body interface{}) (int, []byte) {
	t.Helper()
	return doJSON(t, server, "PUT", path, token, body, "")
}

// AuthPatch performs an authenticated PATCH request.
func AuthPatch(t *testing.T, server *httptest.Server, path, token string, body interface{}) (int, []byte) {
	t.Helper()
	return doJSON(t, server, "PATCH", path, token, body, "")
}

// AuthPostWithIP performs an authenticated POST request with a specific X-Forwarded-For IP.
func AuthPostWithIP(t *testing.T, server *httptest.Server, path, token string, body interface{}, forwardedFor string) (int, []byte) {
	t.Helper()
	return doJSON(t, server, "POST", path, token, body, forwardedFor)
}

// PostWithIP performs a POST request (no auth) with a specific X-Forwarded-For IP.
func PostWithIP(t *testing.T, server *httptest.Server, path, forwardedFor string, body interface{}) (int, []byte) {
	t.Helper()
	return doJSON(t, server, "POST", path, "", body, forwardedFor)
}

// RefreshWithCookies calls POST /api/v1/auth/refresh with the given cookie jar string.
func RefreshWithCookies(t *testing.T, server *httptest.Server, cookieStr string) (int, []byte) {
	t.Helper()
	return doJSONWithCookie(t, server, "POST", "/api/v1/auth/refresh", cookieStr, nil)
}

// LogoutWithCookies calls POST /api/v1/auth/logout with the given cookie jar string.
func LogoutWithCookies(t *testing.T, server *httptest.Server, cookieStr string) (int, []byte) {
	t.Helper()
	return doJSONWithCookie(t, server, "POST", "/api/v1/auth/logout", cookieStr, nil)
}

// LoginUserWithDeviceName logs in a user with a specific device name via HTTP.
func LoginUserWithDeviceName(t *testing.T, server *httptest.Server, username, password, deviceName string) (int, []byte, []string) {
	t.Helper()
	body := map[string]string{
		"username": username,
		"password": password,
	}

	reqBody, _ := json.Marshal(body)
	req, err := http.NewRequest("POST", server.URL+"/api/v1/auth/login", bytes.NewReader(reqBody))
	if err != nil {
		t.Fatalf("failed to create request: %v", err)
	}
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("X-Device-Name", deviceName)

	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatalf("failed to execute request: %v", err)
	}
	defer resp.Body.Close()

	respBody, err := io.ReadAll(resp.Body)
	if err != nil {
		t.Fatalf("failed to read response body: %v", err)
	}

	return resp.StatusCode, respBody, resp.Header.Values("Set-Cookie")
}

// RegisterAndLoginWithDevice registers then logs in with a specific device name.
func RegisterAndLoginWithDevice(t *testing.T, server *httptest.Server, username, email, password, deviceName string) (token, deviceID string) {
	t.Helper()
	statusCode, _ := RegisterUser(t, server, username, email, password)
	if statusCode != http.StatusOK {
		t.Fatalf("expected register status 200, got %d", statusCode)
	}

	statusCode, loginRespBody, _ := LoginUserWithDeviceName(t, server, username, password, deviceName)
	if statusCode != http.StatusOK {
		t.Fatalf("expected login status 200, got %d", statusCode)
	}

	_, _, data := ParseResponse(t, loginRespBody)

	token, _ = data["device_token"].(string)
	deviceID, _ = data["device_id"].(string)

	if token == "" {
		t.Fatal("login response missing device_token")
	}

	return token, deviceID
}

// ParseResponse parses a JSON response and returns the code, message, and data fields.
func ParseResponse(t *testing.T, body []byte) (code int, message string, data map[string]interface{}) {
	t.Helper()
	var resp map[string]interface{}
	if err := json.Unmarshal(body, &resp); err != nil {
		t.Fatalf("failed to unmarshal response: %v\nbody: %s", err, string(body))
	}

	codeFloat, _ := resp["code"].(float64)
	code = int(codeFloat)

	message, _ = resp["message"].(string)

	if d, ok := resp["data"].(map[string]interface{}); ok {
		data = d
	}

	return code, message, data
}

// doJSON performs a JSON HTTP request with optional auth token and optional forwarded-for IP.
func doJSON(t *testing.T, server *httptest.Server, method, path, token string, body interface{}, forwardedFor string) (int, []byte) {
	t.Helper()

	var reqBody io.Reader
	if body != nil {
		jsonBytes, err := json.Marshal(body)
		if err != nil {
			t.Fatalf("failed to marshal request body: %v", err)
		}
		reqBody = bytes.NewReader(jsonBytes)
	}

	req, err := http.NewRequest(method, server.URL+path, reqBody)
	if err != nil {
		t.Fatalf("failed to create request: %v", err)
	}
	req.Header.Set("Content-Type", "application/json")

	if token != "" {
		req.Header.Set("Authorization", "Bearer "+token)
	}

	if forwardedFor != "" {
		req.Header.Set("X-Forwarded-For", forwardedFor)
	}

	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatalf("failed to execute request: %v", err)
	}
	defer resp.Body.Close()

	respBody, err := io.ReadAll(resp.Body)
	if err != nil {
		t.Fatalf("failed to read response body: %v", err)
	}

	return resp.StatusCode, respBody
}

// doJSONWithCookie performs a JSON HTTP request with Cookie header (for refresh/logout).
func doJSONWithCookie(t *testing.T, server *httptest.Server, method, path, cookieStr string, body interface{}) (int, []byte) {
	t.Helper()

	var reqBody io.Reader
	if body != nil {
		jsonBytes, err := json.Marshal(body)
		if err != nil {
			t.Fatalf("failed to marshal request body: %v", err)
		}
		reqBody = bytes.NewReader(jsonBytes)
	}

	req, err := http.NewRequest(method, server.URL+path, reqBody)
	if err != nil {
		t.Fatalf("failed to create request: %v", err)
	}
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("Cookie", cookieStr)

	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatalf("failed to execute request: %v", err)
	}
	defer resp.Body.Close()

	respBody, err := io.ReadAll(resp.Body)
	if err != nil {
		t.Fatalf("failed to read response body: %v", err)
	}

	return resp.StatusCode, respBody
}
// H1: Used to extract HttpOnly tokens from login responses.
func doJSONWithCookies(t *testing.T, server *httptest.Server, method, path, token string, body interface{}, forwardedFor string) (int, []byte, []string) {
	t.Helper()

	var reqBody io.Reader
	if body != nil {
		jsonBytes, err := json.Marshal(body)
		if err != nil {
			t.Fatalf("failed to marshal request body: %v", err)
		}
		reqBody = bytes.NewReader(jsonBytes)
	}

	req, err := http.NewRequest(method, server.URL+path, reqBody)
	if err != nil {
		t.Fatalf("failed to create request: %v", err)
	}
	req.Header.Set("Content-Type", "application/json")

	if token != "" {
		req.Header.Set("Authorization", "Bearer "+token)
	}

	if forwardedFor != "" {
		req.Header.Set("X-Forwarded-For", forwardedFor)
	}

	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatalf("failed to execute request: %v", err)
	}
	defer resp.Body.Close()

	respBody, err := io.ReadAll(resp.Body)
	if err != nil {
		t.Fatalf("failed to read response body: %v", err)
	}

	return resp.StatusCode, respBody, resp.Header.Values("Set-Cookie")
}

// GenerateExpiredToken creates a JWT token that has already expired.
// Used for testing token expiration scenarios.
func GenerateExpiredToken(t *testing.T, jwtSecret string, userID uint) string {
	t.Helper()

	token := jwt.NewWithClaims(jwt.SigningMethodHS256, jwt.MapClaims{
		"user_id":   userID,
		"device_id": "test-device",
		"exp":       time.Now().Add(-time.Hour).Unix(),
		"iat":       time.Now().Add(-2 * time.Hour).Unix(),
	})

	signedToken, err := token.SignedString([]byte(jwtSecret))
	if err != nil {
		t.Fatalf("failed to generate expired token: %v", err)
	}

	return signedToken
}
