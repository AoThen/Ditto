package tests

import (
	"testing"
	"time"

	"ditto-cloud-server/internal/model"
	"ditto-cloud-server/internal/service"
	"ditto-cloud-server/tests/testutil"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// TestCleanup_OldClipsDeleted: clips older than MaxClipAge should be deleted
func TestCleanup_OldClipsDeleted(t *testing.T) {
	server, cfg := testutil.SetupTestServer(t)
	defer server.Close()

	// Shorten cleanup interval for testing
	cfg.CleanupInterval = 100 * time.Millisecond
	cfg.MaxClipAge = 200 * time.Millisecond
	cfg.MaxClipsPerUser = 1000

	user := testutil.CreateTestUser(t, server)

	// Get user ID from DB
	var dbUser model.User
	err := testutil.TestDB.Where("username = ?", user.Username).First(&dbUser).Error
	require.NoError(t, err)

	// Create an old clip manually in DB (simulating a clip that's already aged)
	oldClip := model.Clip{
		ID:          "old-clip",
		UserID:      dbUser.ID,
		DeviceID:    user.DeviceID,
		Description: "old clip",
		CRC:         0,
		CreatedAt:   time.Now().Add(-time.Hour),
		UpdatedAt:   time.Now().Add(-time.Hour), // 1 hour old
	}
	err = testutil.TestDB.Create(&oldClip).Error
	require.NoError(t, err)

	// Verify clip exists
	var count int64
	testutil.TestDB.Model(&model.Clip{}).Count(&count)
	assert.Equal(t, int64(1), count, "should have 1 clip before cleanup")

	// Start cleanup service with short interval
	stopCh := make(chan struct{})
	cleanupSvc := service.NewCleanupService(cfg)
	go cleanupSvc.Start(stopCh)

	// Wait for cleanup to run
	time.Sleep(300 * time.Millisecond)

	// Stop cleanup
	close(stopCh)
	cleanupSvc.Wait()

	// Verify old clip was deleted
	testutil.TestDB.Model(&model.Clip{}).Where("id = ?", "old-clip").Count(&count)
	assert.Equal(t, int64(0), count, "old clip should be deleted")
}

// TestCleanup_NewClipsKept: clips newer than MaxClipAge should be kept
func TestCleanup_NewClipsKept(t *testing.T) {
	server, cfg := testutil.SetupTestServer(t)
	defer server.Close()

	cfg.CleanupInterval = 100 * time.Millisecond
	cfg.MaxClipAge = 1 * time.Hour  // Keep clips for 1 hour
	cfg.MaxClipsPerUser = 1000

	user := testutil.CreateTestUser(t, server)

	// Get user ID from DB
	var dbUser model.User
	err := testutil.TestDB.Where("username = ?", user.Username).First(&dbUser).Error
	require.NoError(t, err)

	// Create a new clip
	newClip := model.Clip{
		ID:          "new-clip",
		UserID:      dbUser.ID,
		DeviceID:    user.DeviceID,
		Description: "new clip",
		CRC:         0,
		CreatedAt:   time.Now(),
		UpdatedAt:   time.Now(), // Just created
	}
	err = testutil.TestDB.Create(&newClip).Error
	require.NoError(t, err)

	// Start cleanup
	stopCh := make(chan struct{})
	cleanupSvc := service.NewCleanupService(cfg)
	go cleanupSvc.Start(stopCh)

	// Wait for cleanup to run
	time.Sleep(300 * time.Millisecond)
	close(stopCh)
	cleanupSvc.Wait()

	// Verify new clip is still there
	var count int64
	testutil.TestDB.Model(&model.Clip{}).Where("id = ?", "new-clip").Count(&count)
	assert.Equal(t, int64(1), count, "new clip should be kept")
}

// TestCleanup_UserLimitEnforced: users exceeding MaxClipsPerUser should have oldest clips removed
func TestCleanup_UserLimitEnforced(t *testing.T) {
	server, cfg := testutil.SetupTestServer(t)
	defer server.Close()

	cfg.CleanupInterval = 100 * time.Millisecond
	cfg.MaxClipAge = 24 * time.Hour     // Don't delete by age
	cfg.MaxClipsPerUser = 3             // Limit to 3 clips

	user := testutil.CreateTestUser(t, server)

	// Get user ID from DB
	var dbUser model.User
	err := testutil.TestDB.Where("username = ?", user.Username).First(&dbUser).Error
	require.NoError(t, err)

	// Create 5 clips (exceeds limit of 3)
	for i := 0; i < 5; i++ {
		clip := model.Clip{
			ID:          "clip-limit-" + string(rune('a'+i)),
			UserID:      dbUser.ID,
			DeviceID:    user.DeviceID,
			Description: "clip",
			CRC:         int64(i),
			CreatedAt:   time.Now().Add(time.Duration(i) * time.Minute), // Staggered times
			UpdatedAt:   time.Now().Add(time.Duration(i) * time.Minute),
		}
		err := testutil.TestDB.Create(&clip).Error
		require.NoError(t, err)
	}

	// Verify 5 clips exist
	var count int64
	testutil.TestDB.Model(&model.Clip{}).Where("user_id = ?", dbUser.ID).Count(&count)
	assert.Equal(t, int64(5), count, "should have 5 clips before cleanup")

	// Start cleanup
	stopCh := make(chan struct{})
	cleanupSvc := service.NewCleanupService(cfg)
	go cleanupSvc.Start(stopCh)

	// Wait for cleanup to run
	time.Sleep(300 * time.Millisecond)
	close(stopCh)
	cleanupSvc.Wait()

	// Verify only 3 clips remain (the newest ones)
	testutil.TestDB.Model(&model.Clip{}).Where("user_id = ?", dbUser.ID).Count(&count)
	assert.Equal(t, int64(3), count, "should have 3 clips after cleanup (limit enforced)")
}
