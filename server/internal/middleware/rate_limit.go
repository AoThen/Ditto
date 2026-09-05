package middleware

import (
	"fmt"
	"net/http"
	"sync"
	"time"

	"ditto-cloud-server/internal/database"
	"ditto-cloud-server/internal/model"
	"ditto-cloud-server/internal/response"

	"github.com/gin-gonic/gin"
)

const (
	// IP-based rate limit
	ipMaxFailures   = 5
	ipBanDuration   = 15 * time.Minute

	// User-based rate limit
	userMaxFailures  = 10
	userLockDuration = 1 * time.Hour

	// failCountWindow is the sliding window behind the failure counters.
	// Without it, fail_count only ever grew: a counter written days ago would
	// still trip the ban, and SyncRateLimit would lock a user out forever.
	failCountWindow = 60 * time.Second
)

type rateLimitCacheEntry struct {
	record    *model.RateLimitRecord
	expiresAt time.Time
}

type RateLimiter struct {
	mu    sync.Mutex
	cache sync.Map
}

func NewRateLimiter() *RateLimiter {
	return &RateLimiter{}
}

func (rl *RateLimiter) LoginRateLimit() gin.HandlerFunc {
	return func(c *gin.Context) {
		rl.mu.Lock()

		clientIP := c.ClientIP()
		ipKey := "login:ip:" + clientIP

		// Check IP ban status
		record := rl.getRecord(ipKey)
		if record != nil && record.BanUntil != nil && record.BanUntil.After(time.Now()) {
			rl.mu.Unlock()
			remaining := record.BanUntil.Sub(time.Now()).Minutes()
			msg := fmt.Sprintf("尝试次数过多，请 %.0f 分钟后重试", remaining)
			if remaining < 1 {
				msg = "尝试次数过多，请 1 分钟后重试"
			}
			response.Error(c, http.StatusTooManyRequests, 42901, msg)
			c.Abort()
			return
		}

		// If ban expired, reset the record
		if record != nil && record.BanUntil != nil && record.BanUntil.Before(time.Now()) {
			rl.deleteRecord(ipKey)
			record = nil
		}

		// Check if already banned (reset case)
		if record != nil && record.FailCount >= ipMaxFailures {
			rl.mu.Unlock()
			banUntil := time.Now().Add(ipBanDuration)
			rl.setBanOnly(ipKey, &banUntil)
			response.Error(c, http.StatusTooManyRequests, 42901, "尝试次数过多，请 15 分钟后重试")
			c.Abort()
			return
		}

		rl.mu.Unlock()

		c.Next()
	}
}

func (rl *RateLimiter) RecordLoginFailure(ip, username string) {
	rl.mu.Lock()
	defer rl.mu.Unlock()

	// IP-based tracking — atomic increment at DB level avoids TOCTOU
	ipKey := "login:ip:" + ip
	ipRecord := rl.atomicIncrement(ipKey)

	if ipRecord.FailCount >= ipMaxFailures && ipRecord.BanUntil == nil {
		banUntil := time.Now().Add(ipBanDuration)
		rl.setBanOnly(ipKey, &banUntil)
		ipRecord.BanUntil = &banUntil
	}

	// User-based tracking — atomic increment at DB level avoids TOCTOU
	userKey := "login:user:" + username
	userRecord := rl.atomicIncrement(userKey)

	if userRecord.FailCount >= userMaxFailures && userRecord.BanUntil == nil {
		lockUntil := time.Now().Add(userLockDuration)
		rl.setBanOnly(userKey, &lockUntil)
		userRecord.BanUntil = &lockUntil
	}
}

func (rl *RateLimiter) RecordLoginSuccess(ip, username string) {
	rl.mu.Lock()
	defer rl.mu.Unlock()

	// Reset counters on successful login
	rl.deleteRecord("login:ip:" + ip)
	rl.deleteRecord("login:user:" + username)
}

func (rl *RateLimiter) IsUserLocked(username string) bool {
	rl.mu.Lock()
	defer rl.mu.Unlock()

	userKey := "login:user:" + username
	record := rl.getRecord(userKey)
	if record != nil && record.BanUntil != nil && record.BanUntil.After(time.Now()) {
		return true
	}
	return false
}

func (rl *RateLimiter) atomicIncrement(key string) *model.RateLimitRecord {
	now := time.Now()
	// Sliding window: a row whose last failure is older than failCountWindow is
	// treated as a fresh start instead of being incremented.
	database.DB.Exec(`
		INSERT INTO rate_limit_records (key, fail_count, updated_at)
		VALUES (?, 1, ?)
		ON CONFLICT(key) DO UPDATE SET
			fail_count = CASE
				WHEN rate_limit_records.updated_at <= ? THEN 1
				ELSE rate_limit_records.fail_count + 1
			END,
			updated_at = ?
	`, key, now, now.Add(-failCountWindow), now)

	var record model.RateLimitRecord
	database.DB.Where("key = ?", key).First(&record)

	rl.cache.Store(key, &rateLimitCacheEntry{
		record:    &record,
		expiresAt: time.Now().Add(1 * time.Minute),
	})
	return &record
}

func (rl *RateLimiter) setBanOnly(key string, banUntil *time.Time) {
	database.DB.Model(&model.RateLimitRecord{}).
		Where("key = ?", key).
		Updates(map[string]interface{}{
			"ban_until":  banUntil,
			"updated_at": time.Now(),
		})
}

func (rl *RateLimiter) getRecord(key string) *model.RateLimitRecord {
	if val, ok := rl.cache.Load(key); ok {
		entry := val.(*rateLimitCacheEntry)
		if time.Now().Before(entry.expiresAt) {
			return entry.record
		}
		rl.cache.Delete(key)
	}
	var record model.RateLimitRecord
	if err := database.DB.First(&record, "key = ?", key).Error; err != nil {
		return nil
	}
	rl.cache.Store(key, &rateLimitCacheEntry{
		record:    &record,
		expiresAt: time.Now().Add(1 * time.Minute),
	})
	return &record
}

func (rl *RateLimiter) SyncRateLimit() gin.HandlerFunc {
	return func(c *gin.Context) {
		userID := c.GetUint("user_id")
		key := "sync:user:" + fmt.Sprintf("%d", userID)

		rl.mu.Lock()
		record := rl.getRecord(key)
		// The counter decays inside atomicIncrement, but a blocked request never
		// reaches it, so the gate must also check the window: otherwise one burst
		// of 60 errors would latch the user out permanently.
		if record != nil && record.FailCount >= 60 && time.Since(record.UpdatedAt) < failCountWindow {
			rl.mu.Unlock()
			response.Error(c, http.StatusTooManyRequests, 42902, "同步请求过于频繁，请稍后再试")
			c.Abort()
			return
		}
		rl.mu.Unlock()
		c.Next()
		if c.Writer.Status() >= 400 {
			rl.atomicIncrement(key)
		}
	}
}

func (rl *RateLimiter) deleteRecord(key string) {
	database.DB.Delete(&model.RateLimitRecord{}, "key = ?", key)
	rl.cache.Delete(key)
}
