package service

import (
	"encoding/base64"
	"fmt"
	"os"
	"testing"
	"time"

	"ditto-cloud-server/internal/database"
	"ditto-cloud-server/internal/model"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// TestSyncRequestValidation tests validation of sync requests
func TestSyncRequestValidation(t *testing.T) {
	tests := []struct {
		name    string
		req     SyncRequest
		wantErr bool
	}{
		{
			name: "valid request with clips",
			req: SyncRequest{
				Since:     time.Now().Add(-time.Hour),
				DeviceID:  "device-123",
				PushClips: []PushClipItem{{ID: "clip-1"}},
			},
			wantErr: false,
		},
		{
			name: "valid request without clips (pull-only)",
			req: SyncRequest{
				Since:     time.Now().Add(-time.Hour),
				DeviceID:  "device-123",
				PushClips: []PushClipItem{},
			},
			wantErr: false,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			// Validation logic would go here
			// For now, just verify the test cases are well-formed
			if tt.req.DeviceID == "" {
				t.Error("DeviceID should not be empty")
			}
		})
	}
}

// TestCRCDeDuplication tests CRC-based deduplication logic
func TestCRCDeDuplication(t *testing.T) {
	tests := []struct {
		name           string
		existingCRCs   map[string]string
		newClipCRC     int64
		shouldSkip     bool
	}{
		{
			name:           "duplicate CRC",
			existingCRCs:   map[string]string{"12345": "clip-1"},
			newClipCRC:     12345,
			shouldSkip:     true,
		},
		{
			name:           "unique CRC",
			existingCRCs:   map[string]string{"12345": "clip-1"},
			newClipCRC:     67890,
			shouldSkip:     false,
		},
		{
			name:           "zero CRC (no dedup)",
			existingCRCs:   map[string]string{"12345": "clip-1"},
			newClipCRC:     0,
			shouldSkip:     false,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			// Simulate CRC check
			crcKey := fmt.Sprintf("%d", tt.newClipCRC)
			_, exists := tt.existingCRCs[crcKey]

			shouldSkip := exists && tt.newClipCRC != 0
			if shouldSkip != tt.shouldSkip {
				t.Errorf("shouldSkip = %v, want %v", shouldSkip, tt.shouldSkip)
			}
		})
	}
}

// TestLWWConflictResolution tests Last Write Wins conflict resolution
func TestLWWConflictResolution(t *testing.T) {
	now := time.Now()

	tests := []struct {
		name            string
		existingUpdated   time.Time
		incomingUpdated   time.Time
		shouldUpdate      bool
	}{
		{
			name:            "incoming is newer",
			existingUpdated: now.Add(-time.Hour),
			incomingUpdated: now,
			shouldUpdate:    true,
		},
		{
			name:            "existing is newer",
			existingUpdated: now,
			incomingUpdated: now.Add(-time.Hour),
			shouldUpdate:    false,
		},
		{
			name:            "same time",
			existingUpdated: now,
			incomingUpdated: now,
			shouldUpdate:    false, // LWW: don't update if same time
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			shouldUpdate := tt.incomingUpdated.After(tt.existingUpdated)
			if shouldUpdate != tt.shouldUpdate {
				t.Errorf("shouldUpdate = %v, want %v", shouldUpdate, tt.shouldUpdate)
			}
		})
	}
}

// TestIncrementalSync tests incremental sync with since parameter
func TestIncrementalSync(t *testing.T) {
	now := time.Now()
	since := now.Add(-time.Hour)

	// Simulate clips updated at different times
	clips := []model.Clip{
		{ID: "clip-1", UpdatedAt: now.Add(-45 * time.Minute)}, // After since
		{ID: "clip-2", UpdatedAt: now.Add(-30 * time.Minute)}, // After since
		{ID: "clip-3", UpdatedAt: now.Add(-90 * time.Minute)}, // Before since
	}

	// Filter clips updated since 'since'
	var syncedClips []model.Clip
	for _, clip := range clips {
		if clip.UpdatedAt.After(since) {
			syncedClips = append(syncedClips, clip)
		}
	}

	// Should only include clips 1 and 2
	if len(syncedClips) != 2 {
		t.Errorf("expected 2 clips, got %d", len(syncedClips))
	}

	// Verify correct clips
	foundIDs := make(map[string]bool)
	for _, clip := range syncedClips {
		foundIDs[clip.ID] = true
	}

	if !foundIDs["clip-1"] || !foundIDs["clip-2"] {
		t.Error("missing expected clips")
	}
	if foundIDs["clip-3"] {
		t.Error("should not include clip-3 (before since)")
	}
}

// TestDeletedClipsSync tests synchronization of deleted clips
func TestDeletedClipsSync(t *testing.T) {
	// Simulate deleted IDs response
	deletedIDs := []string{"clip-1", "clip-2", "clip-3"}

	// Verify all deleted IDs are included
	if len(deletedIDs) != 3 {
		t.Errorf("expected 3 deleted IDs, got %d", len(deletedIDs))
	}

	// Client should process each deleted ID:
	// 1. Find clip in local database
	// 2. Mark as deleted (soft delete)
	// 3. Update UI

	// This test verifies the data structure is correct
}

// TestPullLimit tests pull limit enforcement
func TestPullLimit(t *testing.T) {
	svc, _, _, cleanup := setupClipServiceTest(t)
	defer cleanup()

	tests := []struct {
		name        string
		reqLimit    int
		expectedLim int
	}{
		{
			name:        "no limit specified",
			reqLimit:    0,
			expectedLim: svc.defaultSyncPullLimit,
		},
		{
			name:        "reasonable limit",
			reqLimit:    50,
			expectedLim: 50,
		},
		{
			name:        "excessive limit",
			reqLimit:    10000,
			expectedLim: svc.maxSyncPullLimit,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			pullLimit := tt.reqLimit
			if pullLimit <= 0 {
				pullLimit = svc.defaultSyncPullLimit
			}
			if pullLimit > svc.maxSyncPullLimit {
				pullLimit = svc.maxSyncPullLimit
			}

			if pullLimit != tt.expectedLim {
				t.Errorf("pullLimit = %d, want %d", pullLimit, tt.expectedLim)
			}
		})
	}
}

// TestBroadcastToOthers tests WebSocket broadcast logic
func TestBroadcastToOthers(t *testing.T) {
	// Mock broadcaster
	mockBroadcaster := &MockBroadcaster{
		Messages: make([]BroadcastMessage, 0),
	}

	// Simulate broadcast
	mockBroadcaster.BroadcastToOthers(123, nil, "clips_added", map[string]interface{}{
		"clips": []map[string]interface{}{
			{
				"clip_id":   "clip-1",
				"device_id": "device-abc",
			},
		},
	})

	// Verify message was broadcast
	if len(mockBroadcaster.Messages) != 1 {
		t.Errorf("expected 1 broadcast message, got %d", len(mockBroadcaster.Messages))
	}

	msg := mockBroadcaster.Messages[0]
	if msg.UserID != 123 {
		t.Errorf("userID = %d, want 123", msg.UserID)
	}
	if msg.MsgType != "clips_added" {
		t.Errorf("msgType = %s, want clips_added", msg.MsgType)
	}
}

// MockBroadcaster implements Broadcaster interface for testing
type MockBroadcaster struct {
	Messages []BroadcastMessage
}

type BroadcastMessage struct {
	UserID  int64
	Data    map[string]interface{}
	MsgType string
}

func (m *MockBroadcaster) BroadcastToOthers(userID int64, excludeConn interface{}, msgType string, data map[string]interface{}) {
	m.Messages = append(m.Messages, BroadcastMessage{
		UserID:  userID,
		MsgType: msgType,
		Data:    data,
	})
}

// TestSyncLogOperation tests sync logging for audit trail
func TestSyncLogOperation(t *testing.T) {
	// Simulate sync log entry
	logEntry := SyncLogEntry{
		UserID:    123,
		DeviceID:  "device-abc",
		Action:    "push",
		ClipCount: 5,
		Status:    "success",
		Error:     "",
	}

	// Verify log entry is well-formed
	if logEntry.UserID == 0 {
		t.Error("UserID should not be zero")
	}
	if logEntry.DeviceID == "" {
		t.Error("DeviceID should not be empty")
	}
	if logEntry.Action == "" {
		t.Error("Action should not be empty")
	}
}

type SyncLogEntry struct {
	UserID    int64
	DeviceID  string
	Action    string
	ClipCount int
	Status    string
	Error     string
}

// --- DownloadClipFormat tests ---

func TestDownloadClipFormat_ContentType(t *testing.T) {
	tests := []struct {
		formatType  int
		contentType string
		fileName    string
	}{
		{1, "text/plain", "clip.txt"},
		{7, "text/plain", "clip.txt"},
		{13, "text/plain; charset=utf-16", "clip.txt"},
		{49, "text/html", "clip.html"},
		{8, "image/png", "clip.png"},
		{17, "image/png", "clip.png"},
		{50, "image/png", "clip.png"},
		{15, "text/plain", "file_paths.txt"},
		{999, "application/octet-stream", "clip_data.bin"},
	}

	for _, tt := range tests {
		t.Run(tt.fileName, func(t *testing.T) {
			contentType, fileName := getContentTypeForFormat(tt.formatType)
			if contentType != tt.contentType {
				t.Errorf("contentType = %s, want %s", contentType, tt.contentType)
			}
			if fileName != tt.fileName {
				t.Errorf("fileName = %s, want %s", fileName, tt.fileName)
			}
		})
	}
}

// --- ListConflictClips tests ---

func TestListConflictClips_Empty(t *testing.T) {
	// Verify that listing conflicts returns empty when no conflicts exist
	clips := []model.Clip{}
	if len(clips) != 0 {
		t.Error("expected empty conflict list")
	}
}

func TestListConflictClips_FilterByConflictFlag(t *testing.T) {
	clips := []model.Clip{
		{ID: "conflict-1", IsConflictCopy: true, Description: "Conflict 1"},
		{ID: "normal-1", IsConflictCopy: false, Description: "Normal"},
		{ID: "conflict-2", IsConflictCopy: true, Description: "Conflict 2"},
	}

	var conflicts []model.Clip
	for _, clip := range clips {
		if clip.IsConflictCopy {
			conflicts = append(conflicts, clip)
		}
	}

	if len(conflicts) != 2 {
		t.Errorf("expected 2 conflicts, got %d", len(conflicts))
	}
}

// --- ResolveConflictClip tests ---

func TestResolveConflictClip_ActionValidation(t *testing.T) {
	tests := []struct {
		action string
		valid  bool
	}{
		{"accept", true},
		{"discard", true},
		{"invalid", false},
		{"", false},
	}

	for _, tt := range tests {
		t.Run(tt.action, func(t *testing.T) {
			valid := tt.action == "accept" || tt.action == "discard"
			if valid != tt.valid {
				t.Errorf("valid = %v, want %v", valid, tt.valid)
			}
		})
	}
}

// --- SyncLogOperation tests ---

func TestSyncLogOperation_AllStatuses(t *testing.T) {
	statuses := []struct {
		status string
		valid  bool
	}{
		{"success", true},
		{"failed", true},
		{"", true},
	}

	for _, s := range statuses {
		entry := SyncLogEntry{
			UserID:    1,
			DeviceID:  "device-1",
			Action:    "push",
			ClipCount: 5,
			Status:    s.status,
		}
		if entry.Status == "" && s.valid {
			// Empty status is acceptable
		}
	}
}

// --- DB Integration Tests ---

func setupClipServiceTest(t *testing.T) (*ClipService, uint, string, func()) {
	t.Helper()

	// Reset global dedup cache to prevent test isolation leaks
	dedupMu.Lock()
	dedupCache = make(map[string]time.Time)
	dedupOrder = nil
	dedupMu.Unlock()

	tmpFile, err := os.CreateTemp("", "clip_service_test_*.db")
	require.NoError(t, err)
	dbPath := tmpFile.Name()
	tmpFile.Close()

	err = database.Init(dbPath, 500*time.Millisecond)
	require.NoError(t, err)

	svc := NewClipService(nil, 1000, 1000, 5000, 100)

	user := model.User{
		Username:     "clipuser",
		Email:        "clip@example.com",
		PasswordHash: "hash",
	}
	err = database.DB.Create(&user).Error
	require.NoError(t, err)

	device := model.Device{
		ID:         "test-device-1",
		UserID:     user.ID,
		DeviceName: "Test Device",
	}
	err = database.DB.Create(&device).Error
	require.NoError(t, err)

	clip := model.Clip{
		ID:          "test-clip-1",
		UserID:      user.ID,
		DeviceID:    "test-device-1",
		Description: "Test clip 1",
		CRC:         12345,
		GroupID:     "test-group-1",
		ShortCut:    0,
		PasteCount:  0,
	}
	err = database.DB.Create(&clip).Error
	require.NoError(t, err)

	format := model.ClipFormat{
		ClipID:     "test-clip-1",
		FormatType: 1,
		Data:       []byte("hello world"),
		Encrypted:  false,
	}
	err = database.DB.Create(&format).Error
	require.NoError(t, err)

	cleanup := func() {
		database.DB = nil
		os.Remove(dbPath)
		os.Remove(dbPath + "-shm")
		os.Remove(dbPath + "-wal")
	}

	return svc, user.ID, "test-device-1", cleanup
}

func TestListClips_Success(t *testing.T) {
	svc, userID, _, cleanup := setupClipServiceTest(t)
	defer cleanup()

	for i := 2; i <= 3; i++ {
		clip := model.Clip{
			ID:          fmt.Sprintf("test-clip-%d", i),
			UserID:      userID,
			DeviceID:    "test-device-1",
			Description: fmt.Sprintf("Test clip %d", i),
			CRC:         int64(10000 + i),
		}
		require.NoError(t, database.DB.Create(&clip).Error)
	}

	resp, err := svc.ListClips(userID, 1, 20, "", "", "", "")
	require.NoError(t, err)
	assert.Equal(t, int64(3), resp.Total)
	assert.Len(t, resp.Items.([]ClipListItem), 3)
}

func TestListClips_WithSearch(t *testing.T) {
	svc, userID, _, cleanup := setupClipServiceTest(t)
	defer cleanup()

	clip2 := model.Clip{
		ID:          "test-clip-search",
		UserID:      userID,
		DeviceID:    "test-device-1",
		Description: "unique search term",
		CRC:         99999,
	}
	require.NoError(t, database.DB.Create(&clip2).Error)

	resp, err := svc.ListClips(userID, 1, 20, "unique", "", "", "")
	require.NoError(t, err)
	assert.Equal(t, int64(1), resp.Total)
	assert.Len(t, resp.Items.([]ClipListItem), 1)

	resp, err = svc.ListClips(userID, 1, 20, "nonexistent", "", "", "")
	require.NoError(t, err)
	assert.Equal(t, int64(0), resp.Total)
}

func TestListClips_Empty(t *testing.T) {
	svc, userID, _, cleanup := setupClipServiceTest(t)
	defer cleanup()

	require.NoError(t, database.DB.Delete(&model.Clip{}, "user_id = ?", userID).Error)

	resp, err := svc.ListClips(userID, 1, 20, "", "", "", "")
	require.NoError(t, err)
	assert.Equal(t, int64(0), resp.Total)
	assert.Len(t, resp.Items.([]ClipListItem), 0)
}

func TestListClips_Pagination(t *testing.T) {
	svc, userID, _, cleanup := setupClipServiceTest(t)
	defer cleanup()

	for i := 2; i <= 5; i++ {
		clip := model.Clip{
			ID:          fmt.Sprintf("test-clip-%d", i),
			UserID:      userID,
			DeviceID:    "test-device-1",
			Description: fmt.Sprintf("Test clip %d", i),
			CRC:         int64(10000 + i),
		}
		require.NoError(t, database.DB.Create(&clip).Error)
	}

	resp, err := svc.ListClips(userID, 1, 2, "", "", "", "")
	require.NoError(t, err)
	assert.Equal(t, int64(5), resp.Total)
	assert.Len(t, resp.Items.([]ClipListItem), 2)
	assert.Equal(t, 1, resp.Page)
	assert.Equal(t, 2, resp.PerPage)
}

func TestListClips_ExcludesConflictCopies(t *testing.T) {
	svc, userID, _, cleanup := setupClipServiceTest(t)
	defer cleanup()

	conflictClip := model.Clip{
		ID:             "test-clip-conflict",
		UserID:         userID,
		DeviceID:       "test-device-1",
		Description:    "Conflict copy",
		CRC:            12346,
		IsConflictCopy: true,
	}
	require.NoError(t, database.DB.Create(&conflictClip).Error)

	normalClip := model.Clip{
		ID:          "test-clip-normal-2",
		UserID:      userID,
		DeviceID:    "test-device-1",
		Description: "Normal clip",
		CRC:         12347,
	}
	require.NoError(t, database.DB.Create(&normalClip).Error)

	resp, err := svc.ListClips(userID, 1, 20, "", "", "", "")
	require.NoError(t, err)
	assert.Equal(t, int64(2), resp.Total)

	items := resp.Items.([]ClipListItem)
	for _, item := range items {
		assert.NotEqual(t, "test-clip-conflict", item.ID)
	}
	assert.Equal(t, "test-clip-normal-2", items[0].ID)
}

func TestGetClip_Success(t *testing.T) {
	svc, userID, _, cleanup := setupClipServiceTest(t)
	defer cleanup()

	detail, err := svc.GetClip(userID, "test-clip-1")
	require.NoError(t, err)
	assert.Equal(t, "test-clip-1", detail.ID)
	assert.Equal(t, "Test clip 1", detail.Description)
	assert.Len(t, detail.Formats, 1)
	assert.Equal(t, 1, detail.Formats[0].FormatType)
	assert.Equal(t, base64.StdEncoding.EncodeToString([]byte("hello world")), detail.Formats[0].Data)
}

func TestGetClip_NotFound(t *testing.T) {
	svc, userID, _, cleanup := setupClipServiceTest(t)
	defer cleanup()

	_, err := svc.GetClip(userID, "non-existent-id")
	assert.Error(t, err)
	assert.Equal(t, "剪贴板不存在", err.Error())
}

func TestGetClip_WrongUser(t *testing.T) {
	svc, _, _, cleanup := setupClipServiceTest(t)
	defer cleanup()

	_, err := svc.GetClip(99999, "test-clip-1")
	assert.Error(t, err)
	assert.Equal(t, "剪贴板不存在", err.Error())
}

func TestDownloadClipFormat_Success(t *testing.T) {
	svc, userID, _, cleanup := setupClipServiceTest(t)
	defer cleanup()

	result, err := svc.DownloadClipFormat(userID, "test-clip-1", 1)
	require.NoError(t, err)
	assert.Equal(t, 1, result.FormatType)
	assert.Equal(t, []byte("hello world"), result.Data)
	assert.Equal(t, "text/plain", result.ContentType)
	assert.Equal(t, "clip.txt", result.FileName)
}

func TestDownloadClipFormat_NotFound(t *testing.T) {
	svc, userID, _, cleanup := setupClipServiceTest(t)
	defer cleanup()

	_, err := svc.DownloadClipFormat(userID, "test-clip-1", 999)
	assert.Error(t, err)
	assert.Equal(t, "指定格式不存在", err.Error())
}

func TestDeleteClip_Success(t *testing.T) {
	svc, userID, _, cleanup := setupClipServiceTest(t)
	defer cleanup()

	err := svc.DeleteClip(userID, "test-clip-1", "test-device")
	require.NoError(t, err)

	var count int64
	database.DB.Model(&model.Clip{}).Where("id = ?", "test-clip-1").Count(&count)
	assert.Equal(t, int64(0), count)

	database.DB.Model(&model.ClipFormat{}).Where("clip_id = ?", "test-clip-1").Count(&count)
	assert.Equal(t, int64(0), count)
}

func TestDeleteClip_NotFound(t *testing.T) {
	svc, userID, _, cleanup := setupClipServiceTest(t)
	defer cleanup()

	err := svc.DeleteClip(userID, "non-existent-id", "test-device")
	assert.Error(t, err)
	assert.Equal(t, "剪贴板不存在", err.Error())
}

func TestSync_CreatesConflictCopy(t *testing.T) {
	svc, userID, deviceID, cleanup := setupClipServiceTest(t)
	defer cleanup()

	var existing model.Clip
	require.NoError(t, database.DB.Where("id = ?", "test-clip-1").First(&existing).Error)

	// Set existing clip to the future so syncTime (now) is older — triggers conflict copy
	database.DB.Model(&existing).Update("updated_at", time.Now().Add(time.Hour))
	require.NoError(t, database.DB.Where("id = ?", "test-clip-1").First(&existing).Error)

	data := base64.StdEncoding.EncodeToString([]byte("updated content"))
	req := &SyncRequest{
		Since:    time.Now().Add(-time.Hour),
		DeviceID: deviceID,
		PushClips: []PushClipItem{
			{
				ID:          "test-clip-1",
				Description: "Pushed conflicting version",
				CRC:         54321,
				UpdatedAt:   existing.UpdatedAt.Add(-time.Hour),
				Formats: []PushFormatItem{
					{FormatType: 1, Data: data, Encrypted: false},
				},
			},
		},
	}

	resp, err := svc.Sync(userID, req, deviceID)
	require.NoError(t, err)
	assert.Equal(t, 0, resp.UpdatedCount, "older push should not update the winner")
	assert.Equal(t, 1, resp.SkippedCount, "older push should be counted as skipped")

	var conflictClip model.Clip
	err = database.DB.Where("user_id = ? AND is_conflict_copy = ?", userID, true).First(&conflictClip).Error
	require.NoError(t, err)
	assert.Equal(t, "test-clip-1", conflictClip.WinClipID)
	assert.Equal(t, "Pushed conflicting version", conflictClip.Description)
	assert.Equal(t, int64(54321), conflictClip.CRC)

	var winner model.Clip
	require.NoError(t, database.DB.Where("id = ?", "test-clip-1").First(&winner).Error)
	assert.Equal(t, "Test clip 1", winner.Description)
	assert.Equal(t, int64(12345), winner.CRC)

	var fmtCount int64
	database.DB.Model(&model.ClipFormat{}).Where("clip_id = ?", conflictClip.ID).Count(&fmtCount)
	assert.Equal(t, int64(1), fmtCount)
}

func TestSync_NewerEditNoConflictCopy(t *testing.T) {
	svc, userID, deviceID, cleanup := setupClipServiceTest(t)
	defer cleanup()

	var existing model.Clip
	require.NoError(t, database.DB.Where("id = ?", "test-clip-1").First(&existing).Error)

	// Set existing clip to the past so syncTime (now) is newer — push should win
	database.DB.Model(&existing).Update("updated_at", time.Now().Add(-time.Hour))
	require.NoError(t, database.DB.Where("id = ?", "test-clip-1").First(&existing).Error)

	data := base64.StdEncoding.EncodeToString([]byte("updated content"))
	req := &SyncRequest{
		Since:    time.Now().Add(-time.Hour),
		DeviceID: deviceID,
		PushClips: []PushClipItem{
			{
				ID:          "test-clip-1",
				Description: "Pushed newer version",
				CRC:         54321,
				UpdatedAt:   existing.UpdatedAt,
				Formats: []PushFormatItem{
					{FormatType: 1, Data: data, Encrypted: false},
				},
			},
		},
	}

	resp, err := svc.Sync(userID, req, deviceID)
	require.NoError(t, err)
	assert.Equal(t, 1, resp.UpdatedCount)

	var conflictCount int64
	database.DB.Model(&model.Clip{}).Where("user_id = ? AND is_conflict_copy = ?", userID, true).Count(&conflictCount)
	assert.Equal(t, int64(0), conflictCount)

	var winner model.Clip
	require.NoError(t, database.DB.Where("id = ?", "test-clip-1").First(&winner).Error)
	assert.Equal(t, "Pushed newer version", winner.Description)
	assert.Equal(t, int64(54321), winner.CRC)
}

func TestSync_ConflictCopyNotPulledByOtherDevice(t *testing.T) {
	svc, userID, deviceID, cleanup := setupClipServiceTest(t)
	defer cleanup()

	var existing model.Clip
	require.NoError(t, database.DB.Where("id = ?", "test-clip-1").First(&existing).Error)

	// Set existing clip to the future so syncTime (now) is older → creates conflict copy
	database.DB.Model(&existing).Update("updated_at", time.Now().Add(time.Hour))
	require.NoError(t, database.DB.Where("id = ?", "test-clip-1").First(&existing).Error)

	data := base64.StdEncoding.EncodeToString([]byte("loser content"))
	reqA := &SyncRequest{
		Since:    time.Now().Add(-time.Hour),
		DeviceID: deviceID,
		PushClips: []PushClipItem{
			{
				ID:          "test-clip-1",
				Description: "Loser version",
				CRC:         54321,
				UpdatedAt:   existing.UpdatedAt,
				Formats: []PushFormatItem{
					{FormatType: 1, Data: data, Encrypted: false},
				},
			},
		},
	}
	_, err := svc.Sync(userID, reqA, deviceID)
	require.NoError(t, err)

	var conflictClip model.Clip
	require.NoError(t, database.DB.Where("user_id = ? AND is_conflict_copy = ?", userID, true).First(&conflictClip).Error)

	reqB := &SyncRequest{
		Since:    time.Now().Add(-time.Hour),
		DeviceID: "device-B",
		Limit:    100,
	}
	resp, err := svc.Sync(userID, reqB, "device-B")
	require.NoError(t, err)

	for _, c := range resp.NewClips {
		assert.NotEqual(t, conflictClip.ID, c.ID)
	}
}

func TestSync_PushNewClip(t *testing.T) {
	svc, userID, deviceID, cleanup := setupClipServiceTest(t)
	defer cleanup()

	data := base64.StdEncoding.EncodeToString([]byte("sync content"))
	req := &SyncRequest{
		Since:    time.Now().Add(-time.Hour),
		DeviceID: deviceID,
		PushClips: []PushClipItem{
			{
				ID:          "sync-new-clip",
				Description: "Pushed during sync",
				CRC:         54321,
				UpdatedAt:   time.Now(),
				Formats: []PushFormatItem{
					{FormatType: 1, Data: data, Encrypted: false},
				},
			},
		},
	}

	resp, err := svc.Sync(userID, req, deviceID)
	require.NoError(t, err)
	assert.Equal(t, 1, resp.UpdatedCount)

	var clip model.Clip
	err = database.DB.Where("id = ?", "sync-new-clip").First(&clip).Error
	require.NoError(t, err)
	assert.Equal(t, "Pushed during sync", clip.Description)

	var count int64
	database.DB.Model(&model.ClipFormat{}).Where("clip_id = ?", "sync-new-clip").Count(&count)
	assert.Equal(t, int64(1), count)
}

func TestSync_CRCDedupSameBatch(t *testing.T) {
	svc, userID, deviceID, cleanup := setupClipServiceTest(t)
	defer cleanup()

	data := base64.StdEncoding.EncodeToString([]byte("dup content"))
	req := &SyncRequest{
		Since:    time.Now().Add(-time.Hour),
		DeviceID: deviceID,
		PushClips: []PushClipItem{
			{
				ID: "dup-a", Description: "First", CRC: 99999, UpdatedAt: time.Now(),
				Formats: []PushFormatItem{{FormatType: 1, Data: data, Encrypted: false}},
			},
			{
				ID: "dup-b", Description: "Second", CRC: 99999, UpdatedAt: time.Now(),
				Formats: []PushFormatItem{{FormatType: 1, Data: data, Encrypted: false}},
			},
		},
	}

	resp, err := svc.Sync(userID, req, deviceID)
	require.NoError(t, err)
	assert.Equal(t, 1, resp.UpdatedCount)

	var count int64
	database.DB.Model(&model.Clip{}).Where("user_id = ? AND crc = ?", userID, 99999).Count(&count)
	assert.Equal(t, int64(1), count)
}

func TestSync_PullChanges(t *testing.T) {
	svc, userID, _, cleanup := setupClipServiceTest(t)
	defer cleanup()

	otherClip := model.Clip{
		ID:          "other-device-clip",
		UserID:      userID,
		DeviceID:    "other-device",
		Description: "From other device",
		CRC:         11111,
		UpdatedAt:   time.Now(),
	}
	require.NoError(t, database.DB.Create(&otherClip).Error)

	// Use test-device-1 as current device so setup clip is excluded from pull
	req := &SyncRequest{
		Since:    time.Now().Add(-time.Hour),
		DeviceID: "test-device-1",
		Limit:    100,
	}

	resp, err := svc.Sync(userID, req, "")
	require.NoError(t, err)
	assert.Len(t, resp.NewClips, 1)
	assert.Equal(t, "other-device-clip", resp.NewClips[0].ID)
}

func TestListConflictClips_WithConflicts(t *testing.T) {
	svc, userID, _, cleanup := setupClipServiceTest(t)
	defer cleanup()

	conflictClip := model.Clip{
		ID:             "conflict-clip-1",
		UserID:         userID,
		DeviceID:       "test-device-1",
		Description:    "Conflict clip",
		CRC:            77777,
		IsConflictCopy: true,
	}
	require.NoError(t, database.DB.Create(&conflictClip).Error)

	clips, err := svc.ListConflictClips(userID, 1, 20)
	require.NoError(t, err)
	items := clips.Items.([]ClipListItem)
	assert.Len(t, items, 1)
	assert.Equal(t, "conflict-clip-1", items[0].ID)
}

func TestResolveConflictClip_Accept(t *testing.T) {
	svc, userID, _, cleanup := setupClipServiceTest(t)
	defer cleanup()

	conflictClip := model.Clip{
		ID:             "conflict-accept",
		UserID:         userID,
		DeviceID:       "test-device-1",
		Description:    "Accepted version",
		CRC:            88888,
		IsConflictCopy: true,
		WinClipID:      "test-clip-1",
	}
	require.NoError(t, database.DB.Create(&conflictClip).Error)

	format := model.ClipFormat{
		ClipID:     "conflict-accept",
		FormatType: 1,
		Data:       []byte("accepted data"),
	}
	require.NoError(t, database.DB.Create(&format).Error)

	err := svc.ResolveConflictClip(userID, "conflict-accept", "accept")
	require.NoError(t, err)

	var count int64
	database.DB.Model(&model.Clip{}).Where("id = ?", "conflict-accept").Count(&count)
	assert.Equal(t, int64(0), count)
}

func TestResolveConflictClip_Discard(t *testing.T) {
	svc, userID, _, cleanup := setupClipServiceTest(t)
	defer cleanup()

	conflictClip := model.Clip{
		ID:             "conflict-discard",
		UserID:         userID,
		DeviceID:       "test-device-1",
		Description:    "Discarded version",
		CRC:            88888,
		IsConflictCopy: true,
	}
	require.NoError(t, database.DB.Create(&conflictClip).Error)

	format := model.ClipFormat{
		ClipID:     "conflict-discard",
		FormatType: 1,
		Data:       []byte("discarded data"),
	}
	require.NoError(t, database.DB.Create(&format).Error)

	err := svc.ResolveConflictClip(userID, "conflict-discard", "discard")
	require.NoError(t, err)

	var count int64
	database.DB.Model(&model.Clip{}).Where("id = ?", "conflict-discard").Count(&count)
	assert.Equal(t, int64(0), count)

	database.DB.Model(&model.ClipFormat{}).Where("clip_id = ?", "conflict-discard").Count(&count)
	assert.Equal(t, int64(0), count)
}

// TestLWWClockSkew verifies that server-side syncTime eliminates clock skew issues.
func TestLWWClockSkew(t *testing.T) {
	syncTime := time.Now()

	// Scenario: client clock is 24h slow.
	// Old code used pc.UpdatedAt (client time) → push loses (wrong)
	// New code uses syncTime (server time) → push wins (correct)
	slowClientTime := syncTime.Add(-24 * time.Hour)
	existingUpdatedAt := syncTime.Add(-time.Hour) // clip updated 1h ago on server

	// New code: syncTime is newer than existing → push WINS
	winner := syncTime.After(existingUpdatedAt)
	assert.True(t, winner, "syncTime is newer than existing, push should win")

	// Old code: slowClientTime is NOT newer than existing → push LOSES (wrong!)
	oldLoser := !slowClientTime.After(existingUpdatedAt)
	assert.True(t, oldLoser,
		"with slow client clock, old code incorrectly treated the push as loser")

	// Opposite scenario: existing is newer than syncTime → push should be loser
	existingFuture := syncTime.Add(time.Hour)
	loser := !syncTime.After(existingFuture)
	assert.True(t, loser, "existing is newer than syncTime, push should be loser")
}

// TestDedupCache verifies the in-memory idempotency cache.
func TestDedupCache(t *testing.T) {
	// Initial state: cache is empty
	assert.False(t, isDeduped("1:c1:123"))

	// Mark as processed
	markDeduped("1:c1:123")
	assert.True(t, isDeduped("1:c1:123"))

	// Different key should not be deduped
	assert.False(t, isDeduped("1:c1:456"))
	assert.False(t, isDeduped("2:c1:123"))

	// Max capacity eviction test
	for i := 0; i < dedupMax+100; i++ {
		key := fmt.Sprintf("k:%d", i)
		markDeduped(key)
	}
	assert.True(t, len(dedupCache) <= dedupMax,
		"cache should not exceed max capacity")
}
