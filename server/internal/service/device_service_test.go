package service

import (
	"os"
	"testing"
	"time"

	"ditto-cloud-server/internal/database"
	"ditto-cloud-server/internal/model"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// setupDeviceServiceTest creates an isolated test environment for DeviceService tests
func setupDeviceServiceTest(t *testing.T) (*DeviceService, uint, func()) {
	t.Helper()

	// Create temp database file
	tmpFile, err := os.CreateTemp("", "device_service_test_*.db")
	require.NoError(t, err)
	dbPath := tmpFile.Name()
	tmpFile.Close()

	// Initialize database
	err = database.Init(dbPath, 500*time.Millisecond)
	require.NoError(t, err)

	// Create a test user
	user := model.User{
		Username:     "testuser",
		Email:        "test@example.com",
		PasswordHash: "hash",
	}
	err = database.DB.Create(&user).Error
	require.NoError(t, err)

	// Create service
	svc := NewDeviceService()

	cleanup := func() {
		database.DB = nil
		os.Remove(dbPath)
		os.Remove(dbPath + "-shm")
		os.Remove(dbPath + "-wal")
	}

	return svc, user.ID, cleanup
}

func TestNewDeviceService(t *testing.T) {
	svc := NewDeviceService()
	assert.NotNil(t, svc)
}

func TestDeviceService_ListByUser_Success(t *testing.T) {
	svc, userID, cleanup := setupDeviceServiceTest(t)
	defer cleanup()

	// Create test devices
	device1 := model.Device{
		ID:         "device-1",
		UserID:     userID,
		DeviceName: "Device 1",
		LastSeen:   time.Now().Add(-1 * time.Hour),
	}
	device2 := model.Device{
		ID:         "device-2",
		UserID:     userID,
		DeviceName: "Device 2",
		LastSeen:   time.Now(),
	}
	require.NoError(t, database.DB.Create(&device1).Error)
	require.NoError(t, database.DB.Create(&device2).Error)

	// List devices
	result, err := svc.ListByUser(userID, 1, 20)

	assert.NoError(t, err)
	devices := result.Items.([]DeviceInfo)
	assert.Len(t, devices, 2)
	// Should be ordered by last_seen DESC
	assert.Equal(t, "device-2", devices[0].ID)
	assert.Equal(t, "device-1", devices[1].ID)
}

func TestDeviceService_ListByUser_Empty(t *testing.T) {
	svc, userID, cleanup := setupDeviceServiceTest(t)
	defer cleanup()

	// List devices for user with no devices
	result, err := svc.ListByUser(userID, 1, 20)

	assert.NoError(t, err)
	assert.Empty(t, result.Items)
}

func TestDeviceService_ListByUser_MultipleUsers(t *testing.T) {
	svc, userID, cleanup := setupDeviceServiceTest(t)
	defer cleanup()

	// Create another user
	user2 := model.User{
		Username:     "testuser2",
		Email:        "test2@example.com",
		PasswordHash: "hash",
	}
	require.NoError(t, database.DB.Create(&user2).Error)

	// Create devices for both users
	device1 := model.Device{
		ID:         "device-1",
		UserID:     userID,
		DeviceName: "Device 1",
		LastSeen:   time.Now(),
	}
	device2 := model.Device{
		ID:         "device-2",
		UserID:     user2.ID,
		DeviceName: "Device 2",
		LastSeen:   time.Now(),
	}
	require.NoError(t, database.DB.Create(&device1).Error)
	require.NoError(t, database.DB.Create(&device2).Error)

	// List devices for first user
	result, err := svc.ListByUser(userID, 1, 20)

	assert.NoError(t, err)
	devices := result.Items.([]DeviceInfo)
	assert.Len(t, devices, 1)
	assert.Equal(t, "device-1", devices[0].ID)
}

func TestDeviceService_RemoveDevice_Success(t *testing.T) {
	svc, userID, cleanup := setupDeviceServiceTest(t)
	defer cleanup()

	// Create a test device
	device := model.Device{
		ID:         "device-to-remove",
		UserID:     userID,
		DeviceName: "Device to Remove",
		LastSeen:   time.Now(),
	}
	require.NoError(t, database.DB.Create(&device).Error)

	// Remove the device
	err := svc.RemoveDevice(userID, device.ID)
	assert.NoError(t, err)

	// Verify device was removed
	var count int64
	database.DB.Model(&model.Device{}).Where("id = ?", device.ID).Count(&count)
	assert.Equal(t, int64(0), count)
}

func TestDeviceService_RemoveDevice_NotFound(t *testing.T) {
	svc, userID, cleanup := setupDeviceServiceTest(t)
	defer cleanup()

	// Try to remove non-existent device
	err := svc.RemoveDevice(userID, "non-existent-device")
	// GORM Delete doesn't return error if record not found
	assert.NoError(t, err)
}

func TestDeviceService_RemoveDevice_WrongUser(t *testing.T) {
	svc, userID, cleanup := setupDeviceServiceTest(t)
	defer cleanup()

	// Create another user
	user2 := model.User{
		Username:     "testuser2",
		Email:        "test2@example.com",
		PasswordHash: "hash",
	}
	require.NoError(t, database.DB.Create(&user2).Error)

	// Create a device for user2
	device := model.Device{
		ID:         "device-user2",
		UserID:     user2.ID,
		DeviceName: "Device User 2",
		LastSeen:   time.Now(),
	}
	require.NoError(t, database.DB.Create(&device).Error)

	// Try to remove user2's device as user1
	err := svc.RemoveDevice(userID, device.ID)
	// Should not remove the device
	assert.NoError(t, err)

	// Verify device still exists
	var count int64
	database.DB.Model(&model.Device{}).Where("id = ?", device.ID).Count(&count)
	assert.Equal(t, int64(1), count)
}
