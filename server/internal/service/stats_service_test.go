package service

import (
	"os"
	"testing"

	"ditto-cloud-server/internal/database"
	"ditto-cloud-server/internal/model"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

func setupStatsServiceTest(t *testing.T) (*StatsService, uint, func()) {
	t.Helper()

	tmpFile, err := os.CreateTemp("", "stats_service_test_*.db")
	require.NoError(t, err)
	dbPath := tmpFile.Name()
	tmpFile.Close()

	err = database.Init(dbPath)
	require.NoError(t, err)

	user := model.User{
		Username:     "statsuser",
		Email:        "stats@example.com",
		PasswordHash: "hash",
	}
	err = database.DB.Create(&user).Error
	require.NoError(t, err)

	device := model.Device{
		ID:         "stats-device",
		UserID:     user.ID,
		DeviceName: "Stats Device",
	}
	require.NoError(t, database.DB.Create(&device).Error)

	svc := NewStatsService()

	cleanup := func() {
		database.DB = nil
		os.Remove(dbPath)
		os.Remove(dbPath + "-shm")
		os.Remove(dbPath + "-wal")
	}

	return svc, user.ID, cleanup
}

func TestStatsService_GetDeviceStats_Empty(t *testing.T) {
	svc, userID, cleanup := setupStatsServiceTest(t)
	defer cleanup()

	storage, err := svc.GetDeviceStats(userID)

	assert.NoError(t, err)
	assert.Equal(t, int64(0), storage)
}

func TestStatsService_GetDeviceStats_WithClips(t *testing.T) {
	svc, userID, cleanup := setupStatsServiceTest(t)
	defer cleanup()

	clip := model.Clip{
		ID:          "stats-clip-1",
		UserID:      userID,
		DeviceID:    "stats-device",
		Description: "Test clip",
		CRC:         12345,
	}
	require.NoError(t, database.DB.Create(&clip).Error)

	format := model.ClipFormat{
		ClipID:     clip.ID,
		FormatType: 13,
		Data:       []byte("Hello, World!"),
	}
	require.NoError(t, database.DB.Create(&format).Error)

	storage, err := svc.GetDeviceStats(userID)

	assert.NoError(t, err)
	assert.Equal(t, int64(len("Hello, World!")), storage)
}

func TestStatsService_GetDeviceStats_MultipleClips(t *testing.T) {
	svc, userID, cleanup := setupStatsServiceTest(t)
	defer cleanup()

	clip1 := model.Clip{ID: "stats-clip-a", UserID: userID, DeviceID: "stats-device", Description: "A", CRC: 1}
	clip2 := model.Clip{ID: "stats-clip-b", UserID: userID, DeviceID: "stats-device", Description: "B", CRC: 2}
	require.NoError(t, database.DB.Create(&clip1).Error)
	require.NoError(t, database.DB.Create(&clip2).Error)

	data1 := []byte("Data content one")
	format1 := model.ClipFormat{ClipID: clip1.ID, FormatType: 13, Data: data1}
	require.NoError(t, database.DB.Create(&format1).Error)

	data2 := []byte("Data content two that is longer")
	format2 := model.ClipFormat{ClipID: clip2.ID, FormatType: 13, Data: data2}
	require.NoError(t, database.DB.Create(&format2).Error)

	storage, err := svc.GetDeviceStats(userID)

	assert.NoError(t, err)
	assert.Equal(t, int64(len(data1)+len(data2)), storage)
}