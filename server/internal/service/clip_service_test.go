package service

import (
	"encoding/base64"
	"os"
	"testing"
	"time"

	"ditto-cloud-server/internal/database"
	"ditto-cloud-server/internal/model"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// setupClipServiceTest creates an isolated test environment for ClipService tests
func setupClipServiceTest(t *testing.T) (*ClipService, uint, func()) {
	t.Helper()

	// Create temp database file
	tmpFile, err := os.CreateTemp("", "clip_service_test_*.db")
	require.NoError(t, err)
	dbPath := tmpFile.Name()
	tmpFile.Close()

	// Initialize database
	err = database.Init(dbPath)
	require.NoError(t, err)

	// Create test user
	user := model.User{
		Username:     "testuser",
		Email:        "test@example.com",
		PasswordHash: "hashedpassword",
	}
	require.NoError(t, database.DB.Create(&user).Error)

	// Create service (nil broadcaster for unit tests)
	svc := NewClipService(nil)

	cleanup := func() {
		database.DB = nil
		os.Remove(dbPath)
		os.Remove(dbPath + "-shm")
		os.Remove(dbPath + "-wal")
	}

	return svc, user.ID, cleanup
}

// ensureTestDevice ensures a test device exists for foreign key constraints
func ensureTestDevice(userID uint) {
	device := model.Device{
		ID:         "test-device",
		UserID:     userID,
		DeviceName: "test-device-name",
		LastSeen:   time.Now(),
	}
	database.DB.FirstOrCreate(&device, "id = ?", "test-device")

	// Also create other-device for sync tests
	otherDevice := model.Device{
		ID:         "other-device",
		UserID:     userID,
		DeviceName: "other-device-name",
		LastSeen:   time.Now(),
	}
	database.DB.FirstOrCreate(&otherDevice, "id = ?", "other-device")

	// Also create device-1 and device-2 for LWW tests
	device1 := model.Device{
		ID:         "device-1",
		UserID:     userID,
		DeviceName: "device-1-name",
		LastSeen:   time.Now(),
	}
	database.DB.FirstOrCreate(&device1, "id = ?", "device-1")

	device2 := model.Device{
		ID:         "device-2",
		UserID:     userID,
		DeviceName: "device-2-name",
		LastSeen:   time.Now(),
	}
	database.DB.FirstOrCreate(&device2, "id = ?", "device-2")
}

// createTestClip creates a test clip with formats
func createTestClip(t *testing.T, userID uint, clipID, description string) *model.Clip {
	// Ensure devices exist for foreign key constraints
	ensureTestDevice(userID)

	clip := model.Clip{
		ID:          clipID,
		UserID:      userID,
		DeviceID:    "test-device",
		Description: description,
		CRC:         12345,
		CreatedAt:   time.Now(),
		UpdatedAt:   time.Now(),
	}
	require.NoError(t, database.DB.Create(&clip).Error)

	// Add a text format
	format := model.ClipFormat{
		ClipID:     clip.ID,
		FormatType: 1, // CF_TEXT
		Data:       []byte("test data"),
		CreatedAt:  time.Now(),
	}
	require.NoError(t, database.DB.Create(&format).Error)

	return &clip
}

func TestNewClipService(t *testing.T) {
	svc := NewClipService(nil)
	assert.NotNil(t, svc)
	assert.Nil(t, svc.broadcaster)
}

func TestClipService_ListClips_Empty(t *testing.T) {
	svc, userID, cleanup := setupClipServiceTest(t)
	defer cleanup()

	resp, err := svc.ListClips(userID, 1, 20, "", "")

	assert.NoError(t, err)
	assert.NotNil(t, resp)
	assert.Equal(t, int64(0), resp.Total)
	assert.Empty(t, resp.Items)
}

func TestClipService_ListClips_WithClips(t *testing.T) {
	svc, userID, cleanup := setupClipServiceTest(t)
	defer cleanup()

	// Create test clips
	createTestClip(t, userID, "clip-1", "First clip")
	createTestClip(t, userID, "clip-2", "Second clip")

	resp, err := svc.ListClips(userID, 1, 20, "", "")

	assert.NoError(t, err)
	assert.NotNil(t, resp)
	assert.Equal(t, int64(2), resp.Total)
	assert.Len(t, resp.Items, 2)
}

func TestClipService_ListClips_Pagination(t *testing.T) {
	svc, userID, cleanup := setupClipServiceTest(t)
	defer cleanup()

	// Create 25 clips
	for i := 0; i < 25; i++ {
		createTestClip(t, userID, "clip-pagination-"+string(rune('A'+i%26))+string(rune('0'+i/26)), "Clip")
	}

	// First page
	resp, err := svc.ListClips(userID, 1, 10, "", "")
	require.NoError(t, err)
	assert.Equal(t, int64(25), resp.Total)
	assert.Len(t, resp.Items, 10)
	assert.Equal(t, 1, resp.Page)

	// Second page
	resp, err = svc.ListClips(userID, 2, 10, "", "")
	require.NoError(t, err)
	assert.Len(t, resp.Items, 10)
	assert.Equal(t, 2, resp.Page)

	// Third page
	resp, err = svc.ListClips(userID, 3, 10, "", "")
	require.NoError(t, err)
	assert.Len(t, resp.Items, 5)
	assert.Equal(t, 3, resp.Page)
}

func TestClipService_ListClips_Search(t *testing.T) {
	svc, userID, cleanup := setupClipServiceTest(t)
	defer cleanup()

	// Create clips with different descriptions
	createTestClip(t, userID, "clip-1", "Important meeting notes")
	createTestClip(t, userID, "clip-2", "Shopping list")
	createTestClip(t, userID, "clip-3", "Important email draft")

	// Search for "Important"
	resp, err := svc.ListClips(userID, 1, 20, "Important", "")
	require.NoError(t, err)
	assert.Equal(t, int64(2), resp.Total)

	// Search for "Shopping"
	resp, err = svc.ListClips(userID, 1, 20, "Shopping", "")
	require.NoError(t, err)
	assert.Equal(t, int64(1), resp.Total)

	// Search for non-existent
	resp, err = svc.ListClips(userID, 1, 20, "NonExistent", "")
	require.NoError(t, err)
	assert.Equal(t, int64(0), resp.Total)
}

func TestClipService_ListClips_GroupFilter(t *testing.T) {
	svc, userID, cleanup := setupClipServiceTest(t)
	defer cleanup()

	// Ensure device exists
	ensureTestDevice(userID)

	// Create clips with different groups
	clip1 := model.Clip{
		ID:          "clip-group-1",
		UserID:      userID,
		DeviceID:    "test-device",
		Description: "Clip in group A",
		GroupID:     "group-a",
		CreatedAt:   time.Now(),
		UpdatedAt:   time.Now(),
	}
	require.NoError(t, database.DB.Create(&clip1).Error)

	clip2 := model.Clip{
		ID:          "clip-group-2",
		UserID:      userID,
		DeviceID:    "test-device",
		Description: "Clip in group B",
		GroupID:     "group-b",
		CreatedAt:   time.Now(),
		UpdatedAt:   time.Now(),
	}
	require.NoError(t, database.DB.Create(&clip2).Error)

	// Filter by group-a
	resp, err := svc.ListClips(userID, 1, 20, "", "group-a")
	require.NoError(t, err)
	assert.Equal(t, int64(1), resp.Total)
}

func TestClipService_ListClips_DefaultValues(t *testing.T) {
	svc, userID, cleanup := setupClipServiceTest(t)
	defer cleanup()

	createTestClip(t, userID, "clip-1", "Test clip")

	// Page < 1 should default to 1
	resp, err := svc.ListClips(userID, 0, 20, "", "")
	require.NoError(t, err)
	assert.Equal(t, 1, resp.Page)

	// PerPage < 1 should default to 20
	resp, err = svc.ListClips(userID, 1, 0, "", "")
	require.NoError(t, err)
	assert.Equal(t, 20, resp.PerPage)

	// PerPage > 100 should be capped at 100
	resp, err = svc.ListClips(userID, 1, 200, "", "")
	require.NoError(t, err)
	assert.Equal(t, 100, resp.PerPage)
}

func TestClipService_GetClip_Success(t *testing.T) {
	svc, userID, cleanup := setupClipServiceTest(t)
	defer cleanup()

	createTestClip(t, userID, "test-clip-id", "Test clip content")

	clip, err := svc.GetClip(userID, "test-clip-id")

	assert.NoError(t, err)
	assert.NotNil(t, clip)
	assert.Equal(t, "test-clip-id", clip.ID)
	assert.Equal(t, "Test clip content", clip.Description)
	assert.Len(t, clip.Formats, 1)
	assert.Equal(t, 1, clip.Formats[0].FormatType)
	assert.NotEmpty(t, clip.Formats[0].Data) // Base64-encoded
}

func TestClipService_GetClip_NotFound(t *testing.T) {
	svc, userID, cleanup := setupClipServiceTest(t)
	defer cleanup()

	clip, err := svc.GetClip(userID, "non-existent-id")

	assert.Error(t, err)
	assert.Equal(t, "剪贴板不存在", err.Error())
	assert.Nil(t, clip)
}

func TestClipService_GetClip_WrongUser(t *testing.T) {
	svc, userID, cleanup := setupClipServiceTest(t)
	defer cleanup()

	createTestClip(t, userID, "user-clip", "User's clip")

	// Try to access with different user ID
	clip, err := svc.GetClip(userID+1, "user-clip")

	assert.Error(t, err)
	assert.Equal(t, "剪贴板不存在", err.Error())
	assert.Nil(t, clip)
}

func TestClipService_DeleteClip_Success(t *testing.T) {
	svc, userID, cleanup := setupClipServiceTest(t)
	defer cleanup()

	createTestClip(t, userID, "clip-to-delete", "Delete me")

	err := svc.DeleteClip(userID, "clip-to-delete")

	assert.NoError(t, err)

	// Verify clip is deleted
	var count int64
	database.DB.Model(&model.Clip{}).Where("id = ?", "clip-to-delete").Count(&count)
	assert.Equal(t, int64(0), count)

	// Verify formats are also deleted
	database.DB.Model(&model.ClipFormat{}).Where("clip_id = ?", "clip-to-delete").Count(&count)
	assert.Equal(t, int64(0), count)
}

func TestClipService_DeleteClip_NotFound(t *testing.T) {
	svc, userID, cleanup := setupClipServiceTest(t)
	defer cleanup()

	err := svc.DeleteClip(userID, "non-existent")

	assert.Error(t, err)
	assert.Equal(t, "剪贴板不存在", err.Error())
}

func TestClipService_DeleteClip_WrongUser(t *testing.T) {
	svc, userID, cleanup := setupClipServiceTest(t)
	defer cleanup()

	createTestClip(t, userID, "user-clip", "User's clip")

	err := svc.DeleteClip(userID+1, "user-clip")

	assert.Error(t, err)
	assert.Equal(t, "剪贴板不存在", err.Error())
}

func TestClipService_Sync_PushNewClip(t *testing.T) {
	svc, userID, cleanup := setupClipServiceTest(t)
	defer cleanup()

	// Ensure devices exist
	ensureTestDevice(userID)

	req := &SyncRequest{
		Since:    time.Time{},
		DeviceID: "device-1",
		PushClips: []PushClipItem{
			{
				ID:          "new-clip-1",
				Description: "New clip from client",
				CRC:         12345,
				GroupID:     "",
				ShortCut:    0,
				UpdatedAt:   time.Now(),
				Formats: []PushFormatItem{
					{
						FormatType: 1,
						Data:       base64.StdEncoding.EncodeToString([]byte("clip data")),
					},
				},
			},
		},
	}

	resp, err := svc.Sync(userID, req, "device-1")

	assert.NoError(t, err)
	assert.NotNil(t, resp)
	assert.Equal(t, 1, resp.UpdatedCount)
	assert.Equal(t, 0, resp.SkippedCount)

	// Verify clip was created
	var clip model.Clip
	err = database.DB.First(&clip, "id = ?", "new-clip-1").Error
	assert.NoError(t, err)
	assert.Equal(t, "New clip from client", clip.Description)
}

func TestClipService_Sync_PullNewClips(t *testing.T) {
	svc, userID, cleanup := setupClipServiceTest(t)
	defer cleanup()

	// Ensure devices exist
	ensureTestDevice(userID)

	// Create a clip from another device
	clip := model.Clip{
		ID:          "existing-clip",
		UserID:      userID,
		DeviceID:    "other-device",
		Description: "Clip from other device",
		CRC:         12345,
		CreatedAt:   time.Now().Add(-time.Hour),
		UpdatedAt:   time.Now().Add(-time.Hour),
	}
	require.NoError(t, database.DB.Create(&clip).Error)

	req := &SyncRequest{
		Since:     time.Now().Add(-2 * time.Hour),
		DeviceID:  "current-device",
		PushClips: nil,
	}

	resp, err := svc.Sync(userID, req, "current-device")

	assert.NoError(t, err)
	assert.NotNil(t, resp)
	assert.Len(t, resp.NewClips, 1)
	assert.Equal(t, "existing-clip", resp.NewClips[0].ID)
}

func TestClipService_Sync_LWW_NewerWins(t *testing.T) {
	svc, userID, cleanup := setupClipServiceTest(t)
	defer cleanup()

	// Ensure devices exist
	ensureTestDevice(userID)

	// Create existing clip
	now := time.Now()
	clip := model.Clip{
		ID:          "lww-clip",
		UserID:      userID,
		DeviceID:    "device-1",
		Description: "Old description",
		CRC:         111,
		CreatedAt:   now.Add(-time.Hour),
		UpdatedAt:   now.Add(-time.Hour),
	}
	require.NoError(t, database.DB.Create(&clip).Error)

	// Push newer version
	req := &SyncRequest{
		Since:    time.Time{},
		DeviceID: "device-2",
		PushClips: []PushClipItem{
			{
				ID:          "lww-clip",
				Description: "New description",
				CRC:         222,
				UpdatedAt:   now,
				Formats:     []PushFormatItem{},
			},
		},
	}

	resp, err := svc.Sync(userID, req, "device-2")

	assert.NoError(t, err)
	assert.Equal(t, 1, resp.UpdatedCount)

	// Verify clip was updated
	var updated model.Clip
	database.DB.First(&updated, "id = ?", "lww-clip")
	assert.Equal(t, "New description", updated.Description)
	assert.Equal(t, int64(222), updated.CRC)
}

func TestClipService_Sync_LWW_OlderSkipped(t *testing.T) {
	svc, userID, cleanup := setupClipServiceTest(t)
	defer cleanup()

	// Ensure devices exist
	ensureTestDevice(userID)

	// Create existing clip (newer)
	now := time.Now()
	clip := model.Clip{
		ID:          "lww-clip-old",
		UserID:      userID,
		DeviceID:    "device-1",
		Description: "Newer server version",
		CRC:         222,
		CreatedAt:   now,
		UpdatedAt:   now,
	}
	require.NoError(t, database.DB.Create(&clip).Error)

	// Push older version
	req := &SyncRequest{
		Since:    time.Time{},
		DeviceID: "device-2",
		PushClips: []PushClipItem{
			{
				ID:          "lww-clip-old",
				Description: "Older client version",
				CRC:         111,
				UpdatedAt:   now.Add(-time.Hour),
				Formats:     []PushFormatItem{},
			},
		},
	}

	resp, err := svc.Sync(userID, req, "device-2")

	assert.NoError(t, err)
	assert.Equal(t, 1, resp.SkippedCount)

	// Verify clip was NOT updated
	var unchanged model.Clip
	database.DB.First(&unchanged, "id = ?", "lww-clip-old")
	assert.Equal(t, "Newer server version", unchanged.Description)
}

func TestClipService_DownloadClipFormat_Success(t *testing.T) {
	svc, userID, cleanup := setupClipServiceTest(t)
	defer cleanup()

	// Ensure device exists
	ensureTestDevice(userID)

	// Create clip with multiple formats
	clip := model.Clip{
		ID:          "download-clip",
		UserID:      userID,
		DeviceID:    "test-device",
		Description: "Download test",
		CreatedAt:   time.Now(),
		UpdatedAt:   time.Now(),
	}
	require.NoError(t, database.DB.Create(&clip).Error)

	textFormat := model.ClipFormat{
		ClipID:     clip.ID,
		FormatType: 1, // CF_TEXT
		Data:       []byte("Hello World"),
		CreatedAt:  time.Now(),
	}
	require.NoError(t, database.DB.Create(&textFormat).Error)

	result, err := svc.DownloadClipFormat(userID, "download-clip", 1)

	assert.NoError(t, err)
	assert.NotNil(t, result)
	assert.Equal(t, []byte("Hello World"), result.Data)
	assert.Equal(t, 1, result.FormatType)
	assert.Equal(t, "text/plain", result.ContentType)
	assert.Equal(t, "clip.txt", result.FileName)
}

func TestClipService_DownloadClipFormat_ClipNotFound(t *testing.T) {
	svc, userID, cleanup := setupClipServiceTest(t)
	defer cleanup()

	result, err := svc.DownloadClipFormat(userID, "non-existent", 1)

	assert.Error(t, err)
	assert.Equal(t, "剪贴板不存在", err.Error())
	assert.Nil(t, result)
}

func TestClipService_DownloadClipFormat_FormatNotFound(t *testing.T) {
	svc, userID, cleanup := setupClipServiceTest(t)
	defer cleanup()

	createTestClip(t, userID, "clip-with-text", "Test")

	// Try to download non-existent format type
	result, err := svc.DownloadClipFormat(userID, "clip-with-text", 999)

	assert.Error(t, err)
	assert.Equal(t, "指定格式不存在", err.Error())
	assert.Nil(t, result)
}

func TestGetContentTypeForFormat(t *testing.T) {
	tests := []struct {
		formatType      int
		expectedType    string
		expectedFileName string
	}{
		{1, "text/plain", "clip.txt"},           // CF_TEXT
		{7, "text/plain", "clip.txt"},           // CF_OEMTEXT
		{13, "text/plain; charset=utf-16", "clip.txt"}, // CF_UNICODETEXT
		{49, "text/html", "clip.html"},          // HTML
		{8, "image/png", "clip.png"},            // CF_DIB
		{17, "image/png", "clip.png"},           // CF_DIBV5
		{50, "image/png", "clip.png"},           // custom image
		{15, "text/plain", "file_paths.txt"},    // CF_HDROP
		{999, "application/octet-stream", "clip_data.bin"}, // unknown
	}

	for _, tt := range tests {
		t.Run("", func(t *testing.T) {
			contentType, fileName := getContentTypeForFormat(tt.formatType)
			assert.Equal(t, tt.expectedType, contentType)
			assert.Equal(t, tt.expectedFileName, fileName)
		})
	}
}

func TestClipService_ListConflictClips(t *testing.T) {
	svc, userID, cleanup := setupClipServiceTest(t)
	defer cleanup()

	// Ensure device exists
	ensureTestDevice(userID)

	// Create regular clip
	regular := model.Clip{
		ID:          "regular-clip",
		UserID:      userID,
		DeviceID:    "test-device",
		Description: "Regular",
		CreatedAt:   time.Now(),
		UpdatedAt:   time.Now(),
	}
	require.NoError(t, database.DB.Create(&regular).Error)

	// Create conflict clips
	conflict := model.Clip{
		ID:             "conflict-clip",
		UserID:         userID,
		DeviceID:       "test-device",
		Description:    "Conflict",
		IsConflictCopy: true,
		CreatedAt:      time.Now(),
		UpdatedAt:      time.Now(),
	}
	require.NoError(t, database.DB.Create(&conflict).Error)

	clips, err := svc.ListConflictClips(userID)

	assert.NoError(t, err)
	assert.Len(t, clips, 1)
	assert.Equal(t, "conflict-clip", clips[0].ID)
}

func TestClipService_ResolveConflictClip_Discard(t *testing.T) {
	svc, userID, cleanup := setupClipServiceTest(t)
	defer cleanup()

	// Ensure device exists
	ensureTestDevice(userID)

	// Create conflict clip
	conflict := model.Clip{
		ID:             "conflict-to-resolve",
		UserID:         userID,
		DeviceID:       "test-device",
		Description:    "Conflict",
		CRC:            123,
		IsConflictCopy: true,
		CreatedAt:      time.Now(),
		UpdatedAt:      time.Now(),
	}
	require.NoError(t, database.DB.Create(&conflict).Error)

	err := svc.ResolveConflictClip(userID, "conflict-to-resolve", "discard")

	assert.NoError(t, err)

	// Verify conflict clip is deleted
	var count int64
	database.DB.Model(&model.Clip{}).Where("id = ?", "conflict-to-resolve").Count(&count)
	assert.Equal(t, int64(0), count)
}

func TestClipService_ResolveConflictClip_NotFound(t *testing.T) {
	svc, userID, cleanup := setupClipServiceTest(t)
	defer cleanup()

	err := svc.ResolveConflictClip(userID, "non-existent", "discard")

	assert.Error(t, err)
	assert.Equal(t, "冲突剪贴板不存在", err.Error())
}
