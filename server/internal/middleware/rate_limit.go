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

type RateLimiter struct {
	mu sync.Mutex
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
		record := getRecord(ipKey)
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
			deleteRecord(ipKey)
			record = nil
		}

		// Check if already banned (reset case)
		if record != nil && record.FailCount >= ipMaxFailures {
			rl.mu.Unlock()
			banUntil := time.Now().Add(ipBanDuration)
			saveRecord(ipKey, ipMaxFailures, &banUntil)
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
	ipRecord := getRecord(ipKey)
	if ipRecord == nil {
		ipRecord = &model.RateLimitRecord{Key: ipKey}
	}
	ipRecord.FailCount++

	// Ban IP if threshold reached
	if ipRecord.FailCount >= ipMaxFailures {
		banUntil := time.Now().Add(ipBanDuration)
		saveRecord(ipKey, ipRecord.FailCount, &banUntil)
	} else {
		saveRecord(ipKey, ipRecord.FailCount, nil)
	}

	// User-based tracking
	userKey := "login:user:" + username
	userRecord := getRecord(userKey)
	if userRecord == nil {
		userRecord = &model.RateLimitRecord{Key: userKey}
	}
	userRecord.FailCount++

	// Lock user account if threshold reached
	if userRecord.FailCount >= userMaxFailures {
		lockUntil := time.Now().Add(userLockDuration)
		saveRecord(userKey, userRecord.FailCount, &lockUntil)
	} else {
		saveRecord(userKey, userRecord.FailCount, nil)
	}
}

func (rl *RateLimiter) RecordLoginSuccess(ip, username string) {
	rl.mu.Lock()
	defer rl.mu.Unlock()

	// Reset counters on successful login
	deleteRecord("login:ip:" + ip)
	deleteRecord("login:user:" + username)
}

func (rl *RateLimiter) IsUserLocked(username string) bool {
	rl.mu.Lock()
	defer rl.mu.Unlock()

	userKey := "login:user:" + username
	record := getRecord(userKey)
	if record != nil && record.BanUntil != nil && record.BanUntil.After(time.Now()) {
		return true
	}
	return false
}

func getRecord(key string) *model.RateLimitRecord {
	var record model.RateLimitRecord
	if err := database.DB.First(&record, "key = ?", key).Error; err != nil {
		return nil
	}
	return &record
}

func saveRecord(key string, failCount int, banUntil *time.Time) {
	record := model.RateLimitRecord{
		Key:       key,
		FailCount: failCount,
		BanUntil:  banUntil,
		UpdatedAt: time.Now(),
	}
	database.DB.Save(&record)
}

func deleteRecord(key string) {
	database.DB.Delete(&model.RateLimitRecord{}, "key = ?", key)
}
