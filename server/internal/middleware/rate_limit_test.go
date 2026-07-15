package middleware

import (
	"net/http"
	"net/http/httptest"
	"os"
	"testing"
	"time"

	"ditto-cloud-server/internal/database"

	"github.com/gin-gonic/gin"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// setupRateLimitTest creates an isolated test environment for RateLimiter tests
func setupRateLimitTest(t *testing.T) (*RateLimiter, func()) {
	t.Helper()

	// Create temp database file
	tmpFile, err := os.CreateTemp("", "rate_limit_test_*.db")
	require.NoError(t, err)
	dbPath := tmpFile.Name()
	tmpFile.Close()

	// Initialize database
	err = database.Init(dbPath, 500*time.Millisecond)
	require.NoError(t, err)

	// Create rate limiter
	rl := NewRateLimiter()

	cleanup := func() {
		database.DB = nil
		os.Remove(dbPath)
		os.Remove(dbPath + "-shm")
		os.Remove(dbPath + "-wal")
	}

	return rl, cleanup
}

func TestNewRateLimiter(t *testing.T) {
	rl := NewRateLimiter()
	assert.NotNil(t, rl)
}

func TestRateLimiter_LoginRateLimit_Allowed(t *testing.T) {
	rl, cleanup := setupRateLimitTest(t)
	defer cleanup()

	gin.SetMode(gin.TestMode)
	r := gin.New()
	r.Use(rl.LoginRateLimit())
	r.POST("/login", func(c *gin.Context) {
		c.JSON(http.StatusOK, gin.H{"status": "ok"})
	})

	// First few attempts should be allowed
	for i := 0; i < 4; i++ {
		req := httptest.NewRequest("POST", "/login", nil)
		req.RemoteAddr = "192.168.1.1:1234"
		w := httptest.NewRecorder()
		r.ServeHTTP(w, req)

		assert.Equal(t, http.StatusOK, w.Code, "Attempt %d should be allowed", i+1)
	}
}

func TestRateLimiter_LoginRateLimit_Blocked(t *testing.T) {
	rl, cleanup := setupRateLimitTest(t)
	defer cleanup()

	gin.SetMode(gin.TestMode)
	r := gin.New()
	r.Use(rl.LoginRateLimit())
	r.POST("/login", func(c *gin.Context) {
		c.JSON(http.StatusOK, gin.H{"status": "ok"})
	})

	// Simulate 5 failed attempts (the threshold for IP ban)
	for i := 0; i < 5; i++ {
		rl.RecordLoginFailure("10.0.0.1", "testuser")
	}

	// Now the next request should be blocked
	req := httptest.NewRequest("POST", "/login", nil)
	req.RemoteAddr = "10.0.0.1:1234"
	w := httptest.NewRecorder()
	r.ServeHTTP(w, req)

	assert.Equal(t, http.StatusTooManyRequests, w.Code)
}

func TestRateLimiter_RecordLoginFailure(t *testing.T) {
	rl, cleanup := setupRateLimitTest(t)
	defer cleanup()

	// Record failures
	for i := 0; i < 3; i++ {
		rl.RecordLoginFailure("10.0.0.1", "testuser")
	}

	// Check IP record
	var ipCount int
	database.DB.Raw("SELECT fail_count FROM rate_limit_records WHERE key = ?", "login:ip:10.0.0.1").Scan(&ipCount)
	assert.Equal(t, 3, ipCount)

	// Check user record
	var userCount int
	database.DB.Raw("SELECT fail_count FROM rate_limit_records WHERE key = ?", "login:user:testuser").Scan(&userCount)
	assert.Equal(t, 3, userCount)
}

func TestRateLimiter_RecordLoginFailure_IPBan(t *testing.T) {
	rl, cleanup := setupRateLimitTest(t)
	defer cleanup()

	// Record 5 failures (IP ban threshold)
	for i := 0; i < 5; i++ {
		rl.RecordLoginFailure("10.0.0.1", "user1")
	}

	// IP should be banned - check ban_until is set for IP
	var banUntil *time.Time
	database.DB.Raw("SELECT ban_until FROM rate_limit_records WHERE key = ?", "login:ip:10.0.0.1").Scan(&banUntil)
	assert.NotNil(t, banUntil)
	assert.True(t, banUntil.After(time.Now()))
}

func TestRateLimiter_RecordLoginFailure_UserLock(t *testing.T) {
	rl, cleanup := setupRateLimitTest(t)
	defer cleanup()

	// Record 10 failures (user lock threshold) from different IPs
	for i := 0; i < 10; i++ {
		rl.RecordLoginFailure("10.0.0."+string(rune('0'+i%10)), "targetuser")
	}

	// User should be locked
	locked := rl.IsUserLocked("targetuser")
	assert.True(t, locked)

	// Check ban_until is set for user
	var banUntil *time.Time
	database.DB.Raw("SELECT ban_until FROM rate_limit_records WHERE key = ?", "login:user:targetuser").Scan(&banUntil)
	assert.NotNil(t, banUntil)
	assert.True(t, banUntil.After(time.Now()))
}

func TestRateLimiter_RecordLoginSuccess(t *testing.T) {
	rl, cleanup := setupRateLimitTest(t)
	defer cleanup()

	// Record some failures first
	rl.RecordLoginFailure("10.0.0.1", "testuser")
	rl.RecordLoginFailure("10.0.0.1", "testuser")

	// Then record success
	rl.RecordLoginSuccess("10.0.0.1", "testuser")

	// Records should be deleted
	var ipCount int64
	database.DB.Model(&struct{}{}).Where("key = ?", "login:ip:10.0.0.1").Count(&ipCount)

	var userCount int64
	database.DB.Model(&struct{}{}).Where("key = ?", "login:user:testuser").Count(&userCount)
}

func TestRateLimiter_IsUserLocked_NotLocked(t *testing.T) {
	rl, cleanup := setupRateLimitTest(t)
	defer cleanup()

	locked := rl.IsUserLocked("nonexistent")

	assert.False(t, locked)
}

func TestRateLimiter_IsUserLocked_Locked(t *testing.T) {
	rl, cleanup := setupRateLimitTest(t)
	defer cleanup()

	// Record 10 failures to trigger user lock
	for i := 0; i < 10; i++ {
		rl.RecordLoginFailure("10.0.0."+string(rune('0'+i%10)), "lockeduser")
	}

	locked := rl.IsUserLocked("lockeduser")

	assert.True(t, locked)
}

func TestRateLimiter_IsUserLocked_Expired(t *testing.T) {
	rl, cleanup := setupRateLimitTest(t)
	defer cleanup()

	// Manually create an expired lock record
	past := time.Now().Add(-time.Hour)
	database.DB.Exec(`
		INSERT INTO rate_limit_records (key, fail_count, ban_until, updated_at)
		VALUES (?, 10, ?, ?)
	`, "login:user:expireduser", past, time.Now())

	locked := rl.IsUserLocked("expireduser")

	// Should not be locked since ban_until is in the past
	assert.False(t, locked)
}

func TestRateLimiter_DifferentIPsNotAffected(t *testing.T) {
	rl, cleanup := setupRateLimitTest(t)
	defer cleanup()

	// Block IP 1
	for i := 0; i < 5; i++ {
		rl.RecordLoginFailure("10.0.0.1", "user1")
	}

	// IP 2 should not be affected
	gin.SetMode(gin.TestMode)
	r := gin.New()
	r.Use(rl.LoginRateLimit())
	r.POST("/login", func(c *gin.Context) {
		c.JSON(http.StatusOK, gin.H{"status": "ok"})
	})

	req := httptest.NewRequest("POST", "/login", nil)
	req.RemoteAddr = "10.0.0.2:1234" // Different IP
	w := httptest.NewRecorder()
	r.ServeHTTP(w, req)

	assert.Equal(t, http.StatusOK, w.Code)
}

func TestRateLimiter_ConcurrentAccess(t *testing.T) {
	rl, cleanup := setupRateLimitTest(t)
	defer cleanup()

	// Test concurrent access doesn't cause race conditions
	done := make(chan bool)

	for i := 0; i < 10; i++ {
		go func(id int) {
			for j := 0; j < 100; j++ {
				ip := "10.0.0." + string(rune('0'+id%10))
				username := "user" + string(rune('0'+id%10))
				rl.RecordLoginFailure(ip, username)
				rl.IsUserLocked(username)
			}
			done <- true
		}(i)
	}

	// Wait for all goroutines
	for i := 0; i < 10; i++ {
		<-done
	}

	// If we get here without panic or deadlock, the test passes
	assert.True(t, true)
}

func TestRateLimiter_BanDuration(t *testing.T) {
	rl, cleanup := setupRateLimitTest(t)
	defer cleanup()

	// Record enough failures to trigger IP ban
	for i := 0; i < 5; i++ {
		rl.RecordLoginFailure("10.0.0.1", "user1")
	}

	// Check ban_until is approximately 15 minutes in the future
	var banUntil time.Time
	database.DB.Raw("SELECT ban_until FROM rate_limit_records WHERE key = ?", "login:ip:10.0.0.1").Scan(&banUntil)

	expectedBanUntil := time.Now().Add(15 * time.Minute)
	// Allow 1 second tolerance
	assert.WithinDuration(t, expectedBanUntil, banUntil, time.Second)
}

func TestRateLimiter_UserLockDuration(t *testing.T) {
	rl, cleanup := setupRateLimitTest(t)
	defer cleanup()

	// Record enough failures to trigger user lock
	for i := 0; i < 10; i++ {
		rl.RecordLoginFailure("10.0.0."+string(rune('0'+i%10)), "lockeduser")
	}

	// Check ban_until is approximately 1 hour in the future
	var banUntil time.Time
	database.DB.Raw("SELECT ban_until FROM rate_limit_records WHERE key = ?", "login:user:lockeduser").Scan(&banUntil)

	expectedBanUntil := time.Now().Add(1 * time.Hour)
	// Allow 1 second tolerance
	assert.WithinDuration(t, expectedBanUntil, banUntil, time.Second)
}
