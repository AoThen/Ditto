package tests

import (
	"fmt"
	"net/http"
	"testing"
	"time"

	"ditto-cloud-server/tests/testutil"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

func TestStats_Overview(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	user := testutil.CreateTestUser(t, server)

	statusCode, respBody := testutil.AuthGet(t, server, "/api/v1/stats/overview", user.Token)
	assert.Equal(t, http.StatusOK, statusCode)
	code, _, data := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)
	assert.NotNil(t, data["total_clips"])
	assert.NotNil(t, data["today_clips"])
	assert.NotNil(t, data["total_devices"])
	assert.NotNil(t, data["total_storage"])
	assert.NotNil(t, data["storage_mb"])
	assert.NotNil(t, data["max_storage_mb"])
	assert.NotNil(t, data["trend"])
}

func TestStats_SyncLogs_Empty(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	user := testutil.CreateTestUser(t, server)

	statusCode, respBody := testutil.AuthGet(t, server, "/api/v1/stats/sync-logs", user.Token)
	assert.Equal(t, http.StatusOK, statusCode)
	code, _, data := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)
	assert.NotNil(t, data["items"])
	assert.NotNil(t, data["total"])
	assert.NotNil(t, data["page"])
	assert.NotNil(t, data["per_page"])

	items := data["items"].([]interface{})
	assert.Len(t, items, 0)
}

func TestStats_SyncLogs_AfterSync(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	user := testutil.CreateTestUser(t, server)

	clipID := fmt.Sprintf("clip-stats-%d", time.Now().UnixNano())
	createClipViaSync(t, server, user.Token, user.DeviceID, clipID, "Stats clip", "Stats content")

	statusCode, respBody := testutil.AuthGet(t, server, "/api/v1/stats/sync-logs", user.Token)
	assert.Equal(t, http.StatusOK, statusCode)
	code, _, data := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)

	items := data["items"].([]interface{})
	assert.GreaterOrEqual(t, len(items), 1)

	firstLog := items[0].(map[string]interface{})
	assert.Equal(t, "push", firstLog["action"])
	assert.Equal(t, "success", firstLog["status"])
}

func TestStats_SyncLogs_Pagination(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	user := testutil.CreateTestUser(t, server)

	for i := 0; i < 3; i++ {
		clipID := fmt.Sprintf("clip-stats-pag-%d-%d", time.Now().UnixNano(), i)
		createClipViaSync(t, server, user.Token, user.DeviceID, clipID, fmt.Sprintf("Clip %d", i), fmt.Sprintf("Content %d", i))
	}

	statusCode, respBody := testutil.AuthGet(t, server, "/api/v1/stats/sync-logs?page=1&per_page=2", user.Token)
	require.Equal(t, http.StatusOK, statusCode)
	code, _, data := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)
	assert.Equal(t, float64(1), data["page"])
	assert.Equal(t, float64(2), data["per_page"])
}

func TestStats_SyncLogs_DeviceIDFilter(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	user := testutil.CreateTestUser(t, server)

	clipID := fmt.Sprintf("clip-stats-filter-%d", time.Now().UnixNano())
	createClipViaSync(t, server, user.Token, user.DeviceID, clipID, "Filter clip", "Content")

	statusCode, respBody := testutil.AuthGet(t, server, "/api/v1/stats/sync-logs?device_id="+user.DeviceID, user.Token)
	require.Equal(t, http.StatusOK, statusCode)
	code, _, data := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)

	items := data["items"].([]interface{})
	assert.GreaterOrEqual(t, len(items), 1)
}

func TestStats_SyncLogs_InvalidDeviceID(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	user := testutil.CreateTestUser(t, server)

	statusCode, respBody := testutil.AuthGet(t, server, "/api/v1/stats/sync-logs?device_id=nonexistent", user.Token)
	assert.Equal(t, http.StatusOK, statusCode)
	code, _, data := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)

	items := data["items"].([]interface{})
	assert.Len(t, items, 0)
}

func TestStats_OverviewAfterSync(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)

	user := testutil.CreateTestUser(t, server)

	clipID := fmt.Sprintf("clip-ov-%d", time.Now().UnixNano())
	createClipViaSync(t, server, user.Token, user.DeviceID, clipID, "OV clip", "OV content")

	statusCode, respBody := testutil.AuthGet(t, server, "/api/v1/stats/overview", user.Token)
	assert.Equal(t, http.StatusOK, statusCode)
	code, _, data := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)
	assert.Equal(t, float64(1), data["total_clips"])
	assert.Equal(t, float64(1), data["total_devices"])
}

func TestStats_SyncLogs_ActionFilter(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	user := testutil.CreateTestUser(t, server)

	clipID := fmt.Sprintf("clip-stats-action-%d", time.Now().UnixNano())
	createClipViaSync(t, server, user.Token, user.DeviceID, clipID, "Action clip", "Content")

	statusCode, respBody := testutil.AuthGet(t, server, "/api/v1/stats/sync-logs?action=push", user.Token)
	require.Equal(t, http.StatusOK, statusCode)
	code, _, data := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)

	items := data["items"].([]interface{})
	for _, item := range items {
		assert.Equal(t, "push", item.(map[string]interface{})["action"])
	}
}
