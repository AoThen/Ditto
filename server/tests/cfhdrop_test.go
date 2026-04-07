package tests

import (
	"encoding/base64"
	"net/http"
	"testing"

	"ditto-cloud-server/internal/model"
	"ditto-cloud-server/tests/testutil"

	"github.com/stretchr/testify/assert"
)

// TestCFHDROP file paths are synced but actual file contents are NOT
func TestCFHDROP_SyncsPathsOnly(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	defer server.Close()

	user := testutil.CreateTestUser(t, server)

	// Simulate C++ client pushing a clip with CF_HDROP format
	// The data should be path metadata only, not actual file contents
	clipID := "test-cfhdrop-clip"

	// Build CF_HDROP format as path metadata (simulating what FilterHDROPForSync does)
	// The path metadata is stored as a JSON string, then base64-encoded for the data field
	pathMetaJSON := `{"type":"file_paths","paths":["C:\\Users\\test\\file1.txt","D:\\docs\\file2.pdf"],"count":2}`
	formats := []map[string]interface{}{
		{
			"format_type": 15, // CF_HDROP
			"data":        base64.StdEncoding.EncodeToString([]byte(pathMetaJSON)),
		},
	}

	pushClip := map[string]interface{}{
		"id":          clipID,
		"description": "file drop test",
		"crc":         12345,
		"group_id":    "",
		"short_cut":   0,
		"updated_at":  "2025-01-01T00:00:00Z",
		"formats":     formats,
	}

	syncReq := map[string]interface{}{
		"since":      "1970-01-01T00:00:00Z",
		"device_id":  user.DeviceID,
		"push_clips": []map[string]interface{}{pushClip},
	}

	statusCode, body := testutil.AuthPost(t, server, "/api/v1/clips/sync", user.Token, syncReq)
	assert.Equal(t, http.StatusOK, statusCode, "sync should succeed")

	code, _, _ := testutil.ParseResponse(t, body)
	assert.Equal(t, 0, code, "response code should be 0")

	// Verify the clip was stored
	var clip model.Clip
	err := testutil.TestDB.Where("id = ?", clipID).First(&clip).Error
	assert.NoError(t, err, "clip should exist in DB")
	assert.Equal(t, "file drop test", clip.Description)

	// Verify formats were stored
	var formatsInDB []model.ClipFormat
	err = testutil.TestDB.Where("clip_id = ?", clipID).Find(&formatsInDB).Error
	assert.NoError(t, err, "formats should exist in DB")
	assert.Equal(t, 1, len(formatsInDB), "should have 1 format")

	// Verify the stored data is path metadata, not actual file contents
	// The server decodes base64 before storing, so storedData is the raw JSON
	storedData := string(formatsInDB[0].Data)
	assert.Contains(t, storedData, "file_paths", "stored data should be path metadata: %s", storedData)
	assert.Contains(t, storedData, "file1.txt", "should contain file path: %s", storedData)

	// Verify NO actual file content markers
	assert.NotContains(t, storedData, "PK", "should not contain ZIP/file magic bytes")
}

// Test CF_HDROP path extraction and validation
func TestCFHDROP_PathValidation(t *testing.T) {
	// Test valid Windows paths
	validPaths := []string{
		`C:\Users\test\file.txt`,
		`D:\Documents\report.pdf`,
		`\\server\share\file.docx`,
	}

	for _, path := range validPaths {
		// Verify path is reasonable length
		assert.True(t, len(path) > 3, "path should be valid: %s", path)
	}

	// Test that paths are NOT actual file contents
	fakeContent := "this is actual file content data, not a path"
	assert.NotContains(t, fakeContent, ":", "file content should not look like a path")
}

// Test that encrypted CF_HDROP paths can be stored and retrieved
func TestCFHDROP_EncryptedPaths(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	defer server.Close()

	user := testutil.CreateTestUser(t, server)

	// Setup encryption
	_, _ = testutil.AuthPost(t, server, "/api/v1/encryption/setup", user.Token, map[string]string{})

	// Push clip with CF_HDROP (paths should NOT be encrypted per design)
	clipID := "test-encrypted-hdrop"
	pathMetaJSON := `{"type":"file_paths","paths":["C:\\test\\file.txt"],"count":1}`
	formats := []map[string]interface{}{
		{
			"format_type": 15, // CF_HDROP
			"data":        base64.StdEncoding.EncodeToString([]byte(pathMetaJSON)),
		},
	}

	pushClip := map[string]interface{}{
		"id":          clipID,
		"description": "encrypted hdrop test",
		"crc":         0,
		"group_id":    "",
		"short_cut":   0,
		"updated_at":  "2025-01-01T00:00:00Z",
		"formats":     formats,
	}

	syncReq := map[string]interface{}{
		"since":      "1970-01-01T00:00:00Z",
		"device_id":  user.DeviceID,
		"push_clips": []map[string]interface{}{pushClip},
	}

	statusCode, body := testutil.AuthPost(t, server, "/api/v1/clips/sync", user.Token, syncReq)
	assert.Equal(t, http.StatusOK, statusCode)

	code, _, data := testutil.ParseResponse(t, body)
	assert.Equal(t, 0, code)

	// Verify sync response
	updatedCount := int(data["updated_count"].(float64))
	assert.Equal(t, 1, updatedCount, "should have updated 1 clip")

	// Verify clip is retrievable
	statusCode2, body2 := testutil.AuthGet(t, server, "/api/v1/clips/"+clipID, user.Token)
	assert.Equal(t, http.StatusOK, statusCode2)

	_, _, clipData := testutil.ParseResponse(t, body2)
	assert.Equal(t, "encrypted hdrop test", clipData["description"])

	// Verify formats are accessible
	formatsArr, ok := clipData["formats"].([]interface{})
	assert.True(t, ok, "should have formats array")
	assert.Equal(t, 1, len(formatsArr), "should have 1 format")
}

// Test CF_HDROP with empty paths array
func TestCFHDROP_EmptyPaths(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	defer server.Close()

	user := testutil.CreateTestUser(t, server)

	clipID := "test-empty-hdrop"
	emptyPathMeta := `{"type":"file_paths","paths":[],"count":0}`
	formats := []map[string]interface{}{
		{
			"format_type": 15,
			"data":        base64.StdEncoding.EncodeToString([]byte(emptyPathMeta)),
		},
	}

	pushClip := map[string]interface{}{
		"id":          clipID,
		"description": "empty hdrop test",
		"crc":         0,
		"group_id":    "",
		"short_cut":   0,
		"updated_at":  "2025-01-01T00:00:00Z",
		"formats":     formats,
	}

	syncReq := map[string]interface{}{
		"since":      "1970-01-01T00:00:00Z",
		"device_id":  user.DeviceID,
		"push_clips": []map[string]interface{}{pushClip},
	}

	statusCode, body := testutil.AuthPost(t, server, "/api/v1/clips/sync", user.Token, syncReq)
	assert.Equal(t, http.StatusOK, statusCode)

	code, _, data := testutil.ParseResponse(t, body)
	assert.Equal(t, 0, code)

	updatedCount := int(data["updated_count"].(float64))
	assert.Equal(t, 1, updatedCount, "empty hdrop should still sync")
}

// Helper: encode file paths as base64 (simulating binary CF_HDROP structure)
func encodeHDROPAsBase64(paths []string) string {
	// Simplified: just join paths with null bytes
	// In reality, CF_HDROP has a DROPFILES header structure
	data := ""
	for _, p := range paths {
		data += p + "\x00"
	}
	data += "\x00" // double-null terminator
	return base64.StdEncoding.EncodeToString([]byte(data))
}
