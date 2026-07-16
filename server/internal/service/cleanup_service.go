package service

import (
	"log"
	"time"

	"ditto-cloud-server/internal/config"
	"ditto-cloud-server/internal/database"
	"ditto-cloud-server/internal/model"
	"gorm.io/gorm"
)

// CleanupService handles periodic removal of old clips.
type CleanupService struct {
	cfg    *config.Config
	doneCh chan struct{}
}

// NewCleanupService creates a new cleanup service.
func NewCleanupService(cfg *config.Config) *CleanupService {
	return &CleanupService{cfg: cfg, doneCh: make(chan struct{})}
}

// Start begins the background cleanup goroutine.
// It runs at the configured interval and removes clips older than MaxClipAge
// and enforces MaxClipsPerUser limit.
func (s *CleanupService) Start(stopCh <-chan struct{}) {
	log.Printf("[Cleanup] Starting cleanup service: interval=%v, maxAge=%v, maxClips=%d, softDeleteRetention=%v",
		s.cfg.CleanupInterval, s.cfg.MaxClipAge, s.cfg.MaxClipsPerUser, s.cfg.SoftDeleteRetention)

	defer close(s.doneCh)

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

// Wait blocks until the cleanup goroutine has exited.
func (s *CleanupService) Wait() {
	<-s.doneCh
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

	// Step 3: Hard-delete records soft-deleted longer than configured retention
	deletedHard, err := s.hardDeleteOldSoftDeleted()
	if err != nil {
		log.Printf("[Cleanup] ERROR hard-deleting old soft-deleted clips: %v", err)
	}

	// Step 4: Clean up sync_logs older than 30 days
	deletedLogs, err := s.deleteOldSyncLogs()
	if err != nil {
		log.Printf("[Cleanup] ERROR deleting old sync logs: %v", err)
	}

	// Step 5: Remove conflict copies older than soft-delete retention
	deletedConflicts, err := s.deleteOldConflictClips()
	if err != nil {
		log.Printf("[Cleanup] ERROR deleting old conflict clips: %v", err)
	}

	elapsed := time.Since(startTime)
	log.Printf("[Cleanup] Cleanup complete: deleted %d (by age) + %d (by limit) + %d (hard delete) + %d (sync logs) + %d (conflicts) in %v",
		deletedByAge, deletedByLimit, deletedHard, deletedLogs, deletedConflicts, elapsed.Round(time.Millisecond))
}

// deleteOldClips removes clips older than MaxClipAge.
func (s *CleanupService) deleteOldClips() (int, error) {
	if database.DB == nil {
		return 0, nil // DB not initialized, skip cleanup
	}

	cutoff := time.Now().Add(-s.cfg.MaxClipAge)

	result := database.DB.Where("is_conflict_copy = ? AND updated_at < ?", false, cutoff).Delete(&model.Clip{})
	if result.Error != nil {
		return 0, result.Error
	}

	return int(result.RowsAffected), nil
}

func (s *CleanupService) deleteOldConflictClips() (int, error) {
	if database.DB == nil {
		return 0, nil
	}
	cutoff := time.Now().Add(-s.cfg.SoftDeleteRetention)
	var ids []string
	if err := database.DB.Model(&model.Clip{}).
		Where("is_conflict_copy = ? AND updated_at < ?", true, cutoff).
		Pluck("id", &ids).Error; err != nil {
		return 0, err
	}
	if len(ids) == 0 {
		return 0, nil
	}
	return s.batchDeleteClips(ids)
}

// enforceUserLimits removes excess clips for users exceeding MaxClipsPerUser.
// Keeps the newest clips and removes the oldest ones.
func (s *CleanupService) enforceUserLimits() (int, error) {
	if database.DB == nil {
		return 0, nil // DB not initialized, skip cleanup
	}

	type UserClipCount struct {
		UserID uint
		Count  int64
	}
	var overLimitUsers []UserClipCount
	if err := database.DB.Model(&model.Clip{}).
		Select("user_id, COUNT(*) as count").
		Where("is_conflict_copy = ?", false).
		Group("user_id").
		Having("COUNT(*) > ?", s.cfg.MaxClipsPerUser).
		Find(&overLimitUsers).Error; err != nil {
		return 0, err
	}

	totalDeleted := 0
	for _, uc := range overLimitUsers {
		excessCount := uc.Count - int64(s.cfg.MaxClipsPerUser)

		var clipIDs []string
		if err := database.DB.Model(&model.Clip{}).
			Select("id").
			Where("user_id = ? AND is_conflict_copy = ?", uc.UserID, false).
			Order("updated_at ASC").
			Limit(int(excessCount)).
			Pluck("id", &clipIDs).Error; err != nil {
			return totalDeleted, err
		}

		if len(clipIDs) == 0 {
			continue
		}

		n, err := s.batchDeleteClips(clipIDs)
		if err != nil {
			log.Printf("[Cleanup] ERROR batch deleting %d clips: %v", len(clipIDs), err)
			continue
		}
		totalDeleted += n
	}

	return totalDeleted, nil
}

func (s *CleanupService) batchDeleteClips(clipIDs []string) (int, error) {
	var total int
	err := database.DB.Transaction(func(tx *gorm.DB) error {
		if err := tx.Where("clip_id IN ?", clipIDs).Delete(&model.ClipFormat{}).Error; err != nil {
			return err
		}
		result := tx.Where("id IN ?", clipIDs).Delete(&model.Clip{})
		if result.Error != nil {
			return result.Error
		}
		total = int(result.RowsAffected)
		return nil
	})
	return total, err
}

// deleteOldSyncLogs removes sync_log entries older than 30 days
func (s *CleanupService) deleteOldSyncLogs() (int, error) {
	if database.DB == nil {
		return 0, nil
	}

	cutoff := time.Now().AddDate(0, 0, -30)

	result := database.DB.Where("synced_at < ?", cutoff).Delete(&model.SyncLog{})
	if result.Error != nil {
		return 0, result.Error
	}

	return int(result.RowsAffected), nil
}

func (s *CleanupService) hardDeleteOldSoftDeleted() (int, error) {
	if database.DB == nil {
		return 0, nil
	}

	threshold := time.Now().Add(-s.cfg.SoftDeleteRetention)

	// Hard-delete orphan ClipFormat records first
	database.DB.Where("clip_id NOT IN (SELECT id FROM clips)").Delete(&model.ClipFormat{})

	// Hard-delete clips soft-deleted longer than configured retention
	result := database.DB.Unscoped().
		Where("deleted_at IS NOT NULL AND deleted_at < ?", threshold).
		Delete(&model.Clip{})
	if result.Error != nil {
		return 0, result.Error
	}

	return int(result.RowsAffected), nil
}
