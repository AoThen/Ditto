package middleware

import (
	"net/http"
	"net/http/httptest"
	"os"
	"testing"
	"time"

	"ditto-cloud-server/internal/config"
	"ditto-cloud-server/internal/database"

	"github.com/gin-gonic/gin"
	"github.com/golang-jwt/jwt/v5"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// setupAuthMiddlewareTest creates an isolated test environment for Auth middleware tests
func setupAuthMiddlewareTest(t *testing.T) (*config.Config, func()) {
	t.Helper()

	// Create temp database file
	tmpFile, err := os.CreateTemp("", "auth_middleware_test_*.db")
	require.NoError(t, err)
	dbPath := tmpFile.Name()
	tmpFile.Close()

	// Initialize database
	err = database.Init(dbPath)
	require.NoError(t, err)

	// Create config
	cfg := &config.Config{
		Port:         "0",
		DatabasePath: dbPath,
		JWTSecret:    "test-jwt-secret-key-for-middleware",
		StartTime:    time.Now(),
	}

	cleanup := func() {
		database.DB = nil
		os.Remove(dbPath)
		os.Remove(dbPath + "-shm")
		os.Remove(dbPath + "-wal")
	}

	return cfg, cleanup
}

// createTestRouter creates a test router with auth middleware
func createTestRouter(cfg *config.Config) *gin.Engine {
	gin.SetMode(gin.TestMode)
	router := gin.New()
	router.Use(Auth(cfg))
	router.GET("/protected", func(c *gin.Context) {
		userID := GetUserID(c)
		deviceID := GetDeviceID(c)
		c.JSON(http.StatusOK, gin.H{
			"user_id":   userID,
			"device_id": deviceID,
		})
	})
	return router
}

// generateTestToken creates a valid JWT token for testing
func generateTestToken(cfg *config.Config, userID uint, deviceID string) (string, error) {
	claims := &Claims{
		UserID:   userID,
		DeviceID: deviceID,
		RegisteredClaims: jwt.RegisteredClaims{
			ExpiresAt: jwt.NewNumericDate(time.Now().Add(30 * 24 * time.Hour)),
			IssuedAt:  jwt.NewNumericDate(time.Now()),
		},
	}

	token := jwt.NewWithClaims(jwt.SigningMethodHS256, claims)
	return token.SignedString([]byte(cfg.JWTSecret))
}

func TestAuth_MissingToken(t *testing.T) {
	cfg, cleanup := setupAuthMiddlewareTest(t)
	defer cleanup()

	router := createTestRouter(cfg)

	req, _ := http.NewRequest("GET", "/protected", nil)
	w := httptest.NewRecorder()
	router.ServeHTTP(w, req)

	assert.Equal(t, http.StatusUnauthorized, w.Code)
	assert.Contains(t, w.Body.String(), "未提供认证令牌")
}

func TestAuth_InvalidTokenFormat(t *testing.T) {
	cfg, cleanup := setupAuthMiddlewareTest(t)
	defer cleanup()

	router := createTestRouter(cfg)

	req, _ := http.NewRequest("GET", "/protected", nil)
	req.Header.Set("Authorization", "InvalidFormat token")
	w := httptest.NewRecorder()
	router.ServeHTTP(w, req)

	assert.Equal(t, http.StatusUnauthorized, w.Code)
	// H1: Invalid format results in "无效的认证令牌" (parsed but invalid)
	assert.Contains(t, w.Body.String(), "无效的认证令牌")
}

func TestAuth_InvalidToken(t *testing.T) {
	cfg, cleanup := setupAuthMiddlewareTest(t)
	defer cleanup()

	router := createTestRouter(cfg)

	req, _ := http.NewRequest("GET", "/protected", nil)
	req.Header.Set("Authorization", "Bearer invalid-token")
	w := httptest.NewRecorder()
	router.ServeHTTP(w, req)

	assert.Equal(t, http.StatusUnauthorized, w.Code)
	assert.Contains(t, w.Body.String(), "无效的认证令牌")
}

func TestAuth_ExpiredToken(t *testing.T) {
	cfg, cleanup := setupAuthMiddlewareTest(t)
	defer cleanup()

	router := createTestRouter(cfg)

	// Create expired token
	claims := &Claims{
		UserID:   1,
		DeviceID: "device-1",
		RegisteredClaims: jwt.RegisteredClaims{
			ExpiresAt: jwt.NewNumericDate(time.Now().Add(-24 * time.Hour)), // Expired
			IssuedAt:  jwt.NewNumericDate(time.Now().Add(-48 * time.Hour)),
		},
	}

	token := jwt.NewWithClaims(jwt.SigningMethodHS256, claims)
	tokenStr, err := token.SignedString([]byte(cfg.JWTSecret))
	require.NoError(t, err)

	req, _ := http.NewRequest("GET", "/protected", nil)
	req.Header.Set("Authorization", "Bearer "+tokenStr)
	w := httptest.NewRecorder()
	router.ServeHTTP(w, req)

	assert.Equal(t, http.StatusUnauthorized, w.Code)
	assert.Contains(t, w.Body.String(), "无效的认证令牌")
}

func TestAuth_WrongSecret(t *testing.T) {
	cfg, cleanup := setupAuthMiddlewareTest(t)
	defer cleanup()

	router := createTestRouter(cfg)

	// Create token with wrong secret
	claims := &Claims{
		UserID:   1,
		DeviceID: "device-1",
		RegisteredClaims: jwt.RegisteredClaims{
			ExpiresAt: jwt.NewNumericDate(time.Now().Add(24 * time.Hour)),
			IssuedAt:  jwt.NewNumericDate(time.Now()),
		},
	}

	token := jwt.NewWithClaims(jwt.SigningMethodHS256, claims)
	tokenStr, err := token.SignedString([]byte("wrong-secret"))
	require.NoError(t, err)

	req, _ := http.NewRequest("GET", "/protected", nil)
	req.Header.Set("Authorization", "Bearer "+tokenStr)
	w := httptest.NewRecorder()
	router.ServeHTTP(w, req)

	assert.Equal(t, http.StatusUnauthorized, w.Code)
	assert.Contains(t, w.Body.String(), "无效的认证令牌")
}

func TestAuth_Success(t *testing.T) {
	cfg, cleanup := setupAuthMiddlewareTest(t)
	defer cleanup()

	router := createTestRouter(cfg)

	// Create valid token
	tokenStr, err := generateTestToken(cfg, 123, "device-456")
	require.NoError(t, err)

	req, _ := http.NewRequest("GET", "/protected", nil)
	req.Header.Set("Authorization", "Bearer "+tokenStr)
	w := httptest.NewRecorder()
	router.ServeHTTP(w, req)

	assert.Equal(t, http.StatusOK, w.Code)
	assert.Contains(t, w.Body.String(), `"user_id":123`)
	assert.Contains(t, w.Body.String(), `"device_id":"device-456"`)
}

func TestGetUserID(t *testing.T) {
	gin.SetMode(gin.TestMode)
	c, _ := gin.CreateTestContext(nil)

	// Test when user_id is not set
	userID := GetUserID(c)
	assert.Equal(t, uint(0), userID)

	// Test when user_id is set
	c.Set("user_id", uint(123))
	userID = GetUserID(c)
	assert.Equal(t, uint(123), userID)
}

func TestGetDeviceID(t *testing.T) {
	gin.SetMode(gin.TestMode)
	c, _ := gin.CreateTestContext(nil)

	// Test when device_id is not set
	deviceID := GetDeviceID(c)
	assert.Empty(t, deviceID)

	// Test when device_id is set
	c.Set("device_id", "device-123")
	deviceID = GetDeviceID(c)
	assert.Equal(t, "device-123", deviceID)
}

func TestGetRawToken(t *testing.T) {
	gin.SetMode(gin.TestMode)

	// Test when Authorization header is not set
	c, _ := gin.CreateTestContext(nil)
	c.Request = &http.Request{
		Header: http.Header{},
	}
	token := GetRawToken(c)
	assert.Empty(t, token)

	// Test when Authorization header is set
	c.Request.Header.Set("Authorization", "Bearer test-token-123")
	token = GetRawToken(c)
	assert.Equal(t, "test-token-123", token)

	// Test when Authorization header has no Bearer prefix
	c.Request.Header.Set("Authorization", "test-token-456")
	token = GetRawToken(c)
	assert.Equal(t, "test-token-456", token)
}

func TestClaims_Structure(t *testing.T) {
	cfg, cleanup := setupAuthMiddlewareTest(t)
	defer cleanup()

	router := createTestRouter(cfg)

	// Create token with custom claims
	claims := &Claims{
		UserID:   999,
		DeviceID: "custom-device",
		RegisteredClaims: jwt.RegisteredClaims{
			ExpiresAt: jwt.NewNumericDate(time.Now().Add(30 * 24 * time.Hour)),
			IssuedAt:  jwt.NewNumericDate(time.Now()),
			Issuer:    "ditto-cloud",
			Subject:   "user-auth",
		},
	}

	token := jwt.NewWithClaims(jwt.SigningMethodHS256, claims)
	tokenStr, err := token.SignedString([]byte(cfg.JWTSecret))
	require.NoError(t, err)

	req, _ := http.NewRequest("GET", "/protected", nil)
	req.Header.Set("Authorization", "Bearer "+tokenStr)
	w := httptest.NewRecorder()
	router.ServeHTTP(w, req)

	assert.Equal(t, http.StatusOK, w.Code)
	assert.Contains(t, w.Body.String(), `"user_id":999`)
	assert.Contains(t, w.Body.String(), `"device_id":"custom-device"`)
}
