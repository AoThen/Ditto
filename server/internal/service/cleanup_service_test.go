package service

import (
	"os"
	"testing"
	"time"

	"ditto-cloud-server/internal/config"
	"ditto-cloud-server/internal/database"
	"ditto-cloud-server/internal/model"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
	"gorm.io/gorm"
)

// setupCleanupServiceTest creates an isolated test environment for CleanupService tests
func setupCleanupServiceTest(t *testing.T) (*CleanupService, func()) {
	t.Helper()

	// Create temp database file
	tmpFile, err := os.CreateTemp("", "cleanup_service_test_*.db")
	require.NoError(t, err)
	dbPath := tmpFile.Name()
	tmpFile.Close()

	// Initialize database
	err = database.Init(dbPath, 500*time.Millisecond)
	require.NoError(t, err)

	// Create config
	cfg := &config.Config{
		Port:                 "0",
		DatabasePath:         dbPath,
		JWTSecret:            "test-jwt-secret-key",
		StartTime:            time.Now(),
		CleanupInterval:      1 * time.Hour,
		MaxClipAge:           24 * time.Hour,
		MaxClipsPerUser:      10,
		SoftDeleteRetention:  7 * 24 * time.Hour,
	}

	// Create service
	svc := NewCleanupService(cfg)

	cleanup := func() {
		database.DB = nil
		os.Remove(dbPath)
		os.Remove(dbPath + "-shm")
		os.Remove(dbPath + "-wal")
	}

	return svc, cleanup
}

// createTestUserAndDevice creates a test user and device for clip tests
func createTestUserAndDevice(t *testing.T) (uint, string) {
	t.Helper()

	// Create a test user
	user := model.User{
		Username:     "testuser",
		Email:        "test@example.com",
		PasswordHash: "hash",
	}
	require.NoError(t, database.DB.Create(&user).Error)

	// Create a test device
	device := model.Device{
		ID:         "test-device",
		UserID:     user.ID,
		DeviceName: "Test Device",
	}
	require.NoError(t, database.DB.Create(&device).Error)

	return user.ID, device.ID
}

func TestNewCleanupService(t *testing.T) {
	cfg := &config.Config{
		CleanupInterval:      1 * time.Hour,
		MaxClipAge:           24 * time.Hour,
		MaxClipsPerUser:      10,
		SoftDeleteRetention:  7 * 24 * time.Hour,
	}
	svc := NewCleanupService(cfg)
	assert.NotNil(t, svc)
	assert.Equal(t, cfg, svc.cfg)
	assert.Equal(t, 7*24*time.Hour, svc.cfg.SoftDeleteRetention)
}

func TestCleanupService_DeleteOldClips_Success(t *testing.T) {
	svc, cleanup := setupCleanupServiceTest(t)
	defer cleanup()

	userID, deviceID := createTestUserAndDevice(t)

	// Create old clip (older than MaxClipAge)
	oldClip := model.Clip{
		ID:          "old-clip",
		UserID:      userID,
		DeviceID:    deviceID,
		Description: "Old Clip",
		CRC:         12345,
		UpdatedAt:   time.Now().Add(-48 * time.Hour), // 48 hours ago
	}
	require.NoError(t, database.DB.Create(&oldClip).Error)

	// Create new clip (within MaxClipAge)
	newClip := model.Clip{
		ID:          "new-clip",
		UserID:      userID,
		DeviceID:    deviceID,
		Description: "New Clip",
		CRC:         12346,
		UpdatedAt:   time.Now().Add(-1 * time.Hour), // 1 hour ago
	}
	require.NoError(t, database.DB.Create(&newClip).Error)

	// Delete old clips
	deleted, err := svc.deleteOldClips()

	assert.NoError(t, err)
	assert.Equal(t, 1, deleted)

	// Verify old clip was deleted and new clip still exists
	var oldClipCount, newClipCount int64
	database.DB.Model(&model.Clip{}).Where("id = ?", oldClip.ID).Count(&oldClipCount)
	database.DB.Model(&model.Clip{}).Where("id = ?", newClip.ID).Count(&newClipCount)
	assert.Equal(t, int64(0), oldClipCount)
	assert.Equal(t, int64(1), newClipCount)
}

func TestCleanupService_DeleteOldClips_NoOldClips(t *testing.T) {
	svc, cleanup := setupCleanupServiceTest(t)
	defer cleanup()

	userID, deviceID := createTestUserAndDevice(t)

	// Create only new clips
	newClip := model.Clip{
		ID:          "new-clip",
		UserID:      userID,
		DeviceID:    deviceID,
		Description: "New Clip",
		CRC:         12345,
		UpdatedAt:   time.Now().Add(-1 * time.Hour),
	}
	require.NoError(t, database.DB.Create(&newClip).Error)

	// Delete old clips
	deleted, err := svc.deleteOldClips()

	assert.NoError(t, err)
	assert.Equal(t, 0, deleted)

	// Verify new clip still exists
	var count int64
	database.DB.Model(&model.Clip{}).Where("id = ?", newClip.ID).Count(&count)
	assert.Equal(t, int64(1), count)
}

func TestCleanupService_EnforceUserLimits_Success(t *testing.T) {
	svc, cleanup := setupCleanupServiceTest(t)
	defer cleanup()

	userID, deviceID := createTestUserAndDevice(t)

	// Create more clips than MaxClipsPerUser (10)
	for i := 0; i < 15; i++ {
		clip := model.Clip{
			ID:          string(rune('a' + i)),
			UserID:      userID,
			DeviceID:    deviceID,
			Description: "Test Clip",
			CRC:         int64(i),
			UpdatedAt:   time.Now().Add(time.Duration(i) * time.Minute),
		}
		require.NoError(t, database.DB.Create(&clip).Error)
	}

	// Enforce user limits
	deleted, err := svc.enforceUserLimits()

	assert.NoError(t, err)
	assert.Equal(t, 5, deleted) // Should delete 5 oldest clips

	// Verify user now has exactly MaxClipsPerUser clips
	var count int64
	database.DB.Model(&model.Clip{}).Where("user_id = ?", userID).Count(&count)
	assert.Equal(t, int64(10), count)
}

func TestCleanupService_EnforceUserLimits_WithinLimit(t *testing.T) {
	svc, cleanup := setupCleanupServiceTest(t)
	defer cleanup()

	userID, deviceID := createTestUserAndDevice(t)

	// Create clips within limit (less than 10)
	for i := 0; i < 5; i++ {
		clip := model.Clip{
			ID:          string(rune('a' + i)),
			UserID:      userID,
			DeviceID:    deviceID,
			Description: "Test Clip",
			CRC:         int64(i),
			UpdatedAt:   time.Now().Add(time.Duration(i) * time.Minute),
		}
		require.NoError(t, database.DB.Create(&clip).Error)
	}

	// Enforce user limits
	deleted, err := svc.enforceUserLimits()

	assert.NoError(t, err)
	assert.Equal(t, 0, deleted) // Should not delete any clips

	// Verify all clips still exist
	var count int64
	database.DB.Model(&model.Clip{}).Where("user_id = ?", userID).Count(&count)
	assert.Equal(t, int64(5), count)
}

func TestCleanupService_EnforceUserLimits_MultipleUsers(t *testing.T) {
	svc, cleanup := setupCleanupServiceTest(t)
	defer cleanup()

	// Create two test users
	user1 := model.User{
		Username:     "testuser1",
		Email:        "test1@example.com",
		PasswordHash: "hash",
	}
	user2 := model.User{
		Username:     "testuser2",
		Email:        "test2@example.com",
		PasswordHash: "hash",
	}
	require.NoError(t, database.DB.Create(&user1).Error)
	require.NoError(t, database.DB.Create(&user2).Error)

	// Create devices for both users
	device1 := model.Device{
		ID:         "device-1",
		UserID:     user1.ID,
		DeviceName: "Device 1",
	}
	device2 := model.Device{
		ID:         "device-2",
		UserID:     user2.ID,
		DeviceName: "Device 2",
	}
	require.NoError(t, database.DB.Create(&device1).Error)
	require.NoError(t, database.DB.Create(&device2).Error)

	// Create 15 clips for user1 (exceeds limit)
	for i := 0; i < 15; i++ {
		clip := model.Clip{
			ID:          string(rune('a' + i)),
			UserID:      user1.ID,
			DeviceID:    device1.ID,
			Description: "Test Clip",
			CRC:         int64(i),
			UpdatedAt:   time.Now().Add(time.Duration(i) * time.Minute),
		}
		require.NoError(t, database.DB.Create(&clip).Error)
	}

	// Create 5 clips for user2 (within limit)
	for i := 0; i < 5; i++ {
		clip := model.Clip{
			ID:          string(rune('p' + i)),
			UserID:      user2.ID,
			DeviceID:    device2.ID,
			Description: "Test Clip",
			CRC:         int64(100 + i),
			UpdatedAt:   time.Now().Add(time.Duration(i) * time.Minute),
		}
		require.NoError(t, database.DB.Create(&clip).Error)
	}

	// Enforce user limits
	deleted, err := svc.enforceUserLimits()

	assert.NoError(t, err)
	assert.Equal(t, 5, deleted) // Should delete 5 clips from user1

	// Verify user1 has 10 clips and user2 still has 5
	var count1, count2 int64
	database.DB.Model(&model.Clip{}).Where("user_id = ?", user1.ID).Count(&count1)
	database.DB.Model(&model.Clip{}).Where("user_id = ?", user2.ID).Count(&count2)
	assert.Equal(t, int64(10), count1)
	assert.Equal(t, int64(5), count2)
}

func TestCleanupService_EnforceUserLimits_KeepsNewest(t *testing.T) {
	svc, cleanup := setupCleanupServiceTest(t)
	defer cleanup()

	userID, deviceID := createTestUserAndDevice(t)

	// Create clips with specific timestamps
	clips := make([]model.Clip, 15)
	for i := 0; i < 15; i++ {
		clips[i] = model.Clip{
			ID:          string(rune('a' + i)),
			UserID:      userID,
			DeviceID:    deviceID,
			Description: "Test Clip",
			CRC:         int64(i),
			UpdatedAt:   time.Now().Add(time.Duration(i) * time.Hour), // 0-14 hours ago
		}
		require.NoError(t, database.DB.Create(&clips[i]).Error)
	}

	// Enforce user limits
	deleted, err := svc.enforceUserLimits()

	assert.NoError(t, err)
	assert.Equal(t, 5, deleted)

	// Verify newest clips are kept (last 10 clips)
	var remainingClips []model.Clip
	database.DB.Where("user_id = ?", userID).Order("updated_at DESC").Find(&remainingClips)
	assert.Len(t, remainingClips, 10)

	// Check that oldest clips were deleted
	var exists bool
	for i := 0; i < 5; i++ {
		database.DB.Model(&model.Clip{}).Where("id = ?", clips[i].ID).Select("count(*) > 0").Find(&exists)
		assert.False(t, exists, "Clip %d should be deleted", i)
	}

	// Check that newest clips are kept
	for i := 5; i < 15; i++ {
		database.DB.Model(&model.Clip{}).Where("id = ?", clips[i].ID).Select("count(*) > 0").Find(&exists)
		assert.True(t, exists, "Clip %d should be kept", i)
	}
}

func TestCleanupService_HardDeleteOldSoftDeleted_BeforeThreshold(t *testing.T) {
	svc, cleanup := setupCleanupServiceTest(t)
	defer cleanup()

	userID, deviceID := createTestUserAndDevice(t)

	// Create a clip soft-deleted longer than threshold (8 days ago)
	oldDeleted := model.Clip{
		ID:          "old-deleted",
		UserID:      userID,
		DeviceID:    deviceID,
		Description: "Old Deleted",
		CRC:         12345,
		UpdatedAt:   time.Now().Add(-10 * 24 * time.Hour),
		DeletedAt:   gorm.DeletedAt{Time: time.Now().Add(-8 * 24 * time.Hour), Valid: true},
	}
	require.NoError(t, database.DB.Create(&oldDeleted).Error)

	// Create a clip soft-deleted within threshold (1 day ago)
	recentDeleted := model.Clip{
		ID:          "recent-deleted",
		UserID:      userID,
		DeviceID:    deviceID,
		Description: "Recent Deleted",
		CRC:         12346,
		UpdatedAt:   time.Now().Add(-2 * 24 * time.Hour),
		DeletedAt:   gorm.DeletedAt{Time: time.Now().Add(-24 * time.Hour), Valid: true},
	}
	require.NoError(t, database.DB.Create(&recentDeleted).Error)

	// Run hard delete
	deleted, err := svc.hardDeleteOldSoftDeleted()

	assert.NoError(t, err)
	assert.Equal(t, 1, deleted)

	// Verify old soft-deleted clip is hard-deleted
	var oldExists bool
	database.DB.Unscoped().Model(&model.Clip{}).Where("id = ?", oldDeleted.ID).Select("count(*) > 0").Find(&oldExists)
	assert.False(t, oldExists, "Old soft-deleted clip should be hard-deleted")

	// Verify recent soft-deleted clip still exists
	var recentExists bool
	database.DB.Unscoped().Model(&model.Clip{}).Where("id = ?", recentDeleted.ID).Select("count(*) > 0").Find(&recentExists)
	assert.True(t, recentExists, "Recent soft-deleted clip should still exist")
}

func TestCleanupService_HardDeleteOldSoftDeleted_NoRecords(t *testing.T) {
	svc, cleanup := setupCleanupServiceTest(t)
	defer cleanup()

	// No clips at all
	deleted, err := svc.hardDeleteOldSoftDeleted()

	assert.NoError(t, err)
	assert.Equal(t, 0, deleted)
}

func TestCleanupService_HardDeleteOldSoftDeleted_OnlyActiveClips(t *testing.T) {
	svc, cleanup := setupCleanupServiceTest(t)
	defer cleanup()

	userID, deviceID := createTestUserAndDevice(t)

	// Create active (non-deleted) clips
	clip := model.Clip{
		ID:          "active-clip",
		UserID:      userID,
		DeviceID:    deviceID,
		Description: "Active Clip",
		CRC:         12345,
		UpdatedAt:   time.Now().Add(-1 * time.Hour),
	}
	require.NoError(t, database.DB.Create(&clip).Error)

	// DeletedAt is nil, should NOT be hard-deleted
	deleted, err := svc.hardDeleteOldSoftDeleted()

	assert.NoError(t, err)
	assert.Equal(t, 0, deleted)

	// Verify active clip still exists
	var exists bool
	database.DB.Unscoped().Model(&model.Clip{}).Where("id = ?", clip.ID).Select("count(*) > 0").Find(&exists)
	assert.True(t, exists, "Active clip should still exist")
}