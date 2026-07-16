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
	ipMaxFailures     = 5
	ipBanDuration     = 15 * time.Minute

	// User-based rate limit
	userMaxFailures   = 10
	userLockDuration  = 1 * time.Hour
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
			rl.saveRecord(ipKey, ipMaxFailures, &banUntil)
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

	// IP-based tracking
	ipKey := "login:ip:" + ip
	ipRecord := rl.getRecord(ipKey)
	if ipRecord == nil {
		ipRecord = &model.RateLimitRecord{Key: ipKey}
	}
	ipRecord.FailCount++

	// Ban IP if threshold reached
	if ipRecord.FailCount >= ipMaxFailures {
		banUntil := time.Now().Add(ipBanDuration)
		rl.saveRecord(ipKey, ipRecord.FailCount, &banUntil)
	} else {
		rl.saveRecord(ipKey, ipRecord.FailCount, nil)
	}

	// User-based tracking
	userKey := "login:user:" + username
	userRecord := rl.getRecord(userKey)
	if userRecord == nil {
		userRecord = &model.RateLimitRecord{Key: userKey}
	}
	userRecord.FailCount++

	// Lock user account if threshold reached
	if userRecord.FailCount >= userMaxFailures {
		lockUntil := time.Now().Add(userLockDuration)
		rl.saveRecord(userKey, userRecord.FailCount, &lockUntil)
	} else {
		rl.saveRecord(userKey, userRecord.FailCount, nil)
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

func (rl *RateLimiter) saveRecord(key string, failCount int, banUntil *time.Time) {
	record := model.RateLimitRecord{
		Key:       key,
		FailCount: failCount,
		BanUntil:  banUntil,
		UpdatedAt: time.Now(),
	}
	database.DB.Save(&record)
	rl.cache.Store(key, &rateLimitCacheEntry{
		record:    &record,
		expiresAt: time.Now().Add(1 * time.Minute),
	})
}

func (rl *RateLimiter) deleteRecord(key string) {
	database.DB.Delete(&model.RateLimitRecord{}, "key = ?", key)
	rl.cache.Delete(key)
}
