package service

import (
	"log"
	"time"

	"ditto-cloud-server/internal/config"
	"ditto-cloud-server/internal/database"
	"ditto-cloud-server/internal/model"
)

// CleanupService handles periodic removal of old clips.
type CleanupService struct {
	cfg *config.Config
}

// NewCleanupService creates a new cleanup service.
func NewCleanupService(cfg *config.Config) *CleanupService {
	return &CleanupService{cfg: cfg}
}

// Start begins the background cleanup goroutine.
// It runs at the configured interval and removes clips older than MaxClipAge
// and enforces MaxClipsPerUser limit.
func (s *CleanupService) Start(stopCh <-chan struct{}) {
	log.Printf("[Cleanup] Starting cleanup service: interval=%v, maxAge=%v, maxClips=%d",
		s.cfg.CleanupInterval, s.cfg.MaxClipAge, s.cfg.MaxClipsPerUser)

	ticker := time.NewTicker(s.cfg.CleanupInterval)
	defer ticker.Stop()

	// Run immediately on startup
	s.runCleanup()

	for {
		select {
		case <-stopCh:
			log.Println("[Cleanup] Stopping cleanup service")
			return
		case <-ticker.C:
			s.runCleanup()
		}
	}
}

// runCleanup performs one cleanup cycle.
func (s *CleanupService) runCleanup() {
	startTime := time.Now()

	// Step 1: Remove clips older than MaxClipAge
	deletedByAge, err := s.deleteOldClips()
	if err != nil {
		log.Printf("[Cleanup] ERROR deleting old clips: %v", err)
	}

	// Step 2: Enforce MaxClipsPerUser limit (keep newest clips)
	deletedByLimit, err := s.enforceUserLimits()
	if err != nil {
		log.Printf("[Cleanup] ERROR enforcing user limits: %v", err)
	}

	elapsed := time.Since(startTime)
	log.Printf("[Cleanup] Cleanup complete: deleted %d (by age) + %d (by limit) clips in %v",
		deletedByAge, deletedByLimit, elapsed.Round(time.Millisecond))
}

// deleteOldClips removes clips older than MaxClipAge.
func (s *CleanupService) deleteOldClips() (int, error) {
	cutoff := time.Now().Add(-s.cfg.MaxClipAge)

	result := database.DB.Where("updated_at < ?", cutoff).Delete(&model.Clip{})
	if result.Error != nil {
		return 0, result.Error
	}

	return int(result.RowsAffected), nil
}

// enforceUserLimits removes excess clips for users exceeding MaxClipsPerUser.
// Keeps the newest clips and removes the oldest ones.
func (s *CleanupService) enforceUserLimits() (int, error) {
	totalDeleted := 0

	// Find users who exceed the limit
	type UserClipCount struct {
		UserID uint
		Count  int64
	}

	var overLimitUsers []UserClipCount
	if err := database.DB.Model(&model.Clip{}).
		Select("user_id, COUNT(*) as count").
		Group("user_id").
		Having("COUNT(*) > ?", s.cfg.MaxClipsPerUser).
		Find(&overLimitUsers).Error; err != nil {
		return 0, err
	}

	for _, uc := range overLimitUsers {
		// Calculate how many clips to delete
		excessCount := uc.Count - int64(s.cfg.MaxClipsPerUser)

		// Delete the oldest clips for this user
		var clipsToDelete []model.Clip
		if err := database.DB.Where("user_id = ?", uc.UserID).
			Order("updated_at ASC").
			Limit(int(excessCount)).
			Find(&clipsToDelete).Error; err != nil {
			return totalDeleted, err
		}

		for _, clip := range clipsToDelete {
			if err := database.DB.Delete(&clip).Error; err != nil {
				log.Printf("[Cleanup] ERROR deleting clip %s: %v", clip.ID, err)
				continue
			}
			totalDeleted++
		}
	}

	return totalDeleted, nil
}
