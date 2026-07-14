package tests

import (
	"encoding/json"
	"fmt"
	"net/http"
	"testing"
	"time"

	"ditto-cloud-server/tests/testutil"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

func TestGroups_List_Empty(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	defer server.Close()

	user := testutil.CreateTestUser(t, server)

	statusCode, respBody := testutil.AuthGet(t, server, "/api/v1/groups", user.Token)
	assert.Equal(t, http.StatusOK, statusCode)
	code, _, _ := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)

	// ListGroups returns data as array directly
	var respMap map[string]interface{}
	json.Unmarshal(respBody, &respMap)
	items, ok := respMap["data"].([]interface{})
	require.True(t, ok)
	assert.Empty(t, items)
}

func TestGroups_CreateAndList(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	defer server.Close()

	user := testutil.CreateTestUser(t, server)

	createBody := map[string]interface{}{
		"name":        "Test Group",
		"description": "A test group",
	}
	statusCode, respBody := testutil.AuthPost(t, server, "/api/v1/groups", user.Token, createBody)
	assert.Equal(t, http.StatusOK, statusCode)
	code, _, data := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)
	assert.NotEmpty(t, data["id"])

	groupID := data["id"].(string)
	assert.NotEmpty(t, groupID)

	statusCode, respBody = testutil.AuthGet(t, server, "/api/v1/groups", user.Token)
	assert.Equal(t, http.StatusOK, statusCode)
	code, _, data = testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)
}

func TestGroups_CreateWithParent(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	defer server.Close()

	user := testutil.CreateTestUser(t, server)

	parentBody := map[string]interface{}{"name": "Parent Group"}
	statusCode, respBody := testutil.AuthPost(t, server, "/api/v1/groups", user.Token, parentBody)
	require.Equal(t, http.StatusOK, statusCode)
	code, _, parentData := testutil.ParseResponse(t, respBody)
	require.Equal(t, 0, code)
	parentID := parentData["id"].(string)

	childBody := map[string]interface{}{
		"name":      "Child Group",
		"parent_id": parentID,
	}
	statusCode, respBody = testutil.AuthPost(t, server, "/api/v1/groups", user.Token, childBody)
	assert.Equal(t, http.StatusOK, statusCode)
	code, _, childData := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)
	assert.NotEmpty(t, childData["id"])
}

func TestGroups_GetGroup(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	defer server.Close()

	user := testutil.CreateTestUser(t, server)

	createBody := map[string]interface{}{"name": "Group to Get"}
	statusCode, respBody := testutil.AuthPost(t, server, "/api/v1/groups", user.Token, createBody)
	require.Equal(t, http.StatusOK, statusCode)
	code, _, data := testutil.ParseResponse(t, respBody)
	require.Equal(t, 0, code)
	groupID := data["id"].(string)

	statusCode, respBody = testutil.AuthGet(t, server, "/api/v1/groups/"+groupID, user.Token)
	assert.Equal(t, http.StatusOK, statusCode)
	code, _, data = testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)
	assert.Equal(t, groupID, data["id"])
	assert.Equal(t, "Group to Get", data["name"])
}

func TestGroups_GetGroup_NotFound(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	defer server.Close()

	user := testutil.CreateTestUser(t, server)

	statusCode, respBody := testutil.AuthGet(t, server, "/api/v1/groups/non-existent", user.Token)
	assert.Equal(t, http.StatusNotFound, statusCode)
	code, _, _ := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 40400, code)
}

func TestGroups_Update(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	defer server.Close()

	user := testutil.CreateTestUser(t, server)

	createBody := map[string]interface{}{"name": "Original Name"}
	statusCode, respBody := testutil.AuthPost(t, server, "/api/v1/groups", user.Token, createBody)
	require.Equal(t, http.StatusOK, statusCode)
	code, _, data := testutil.ParseResponse(t, respBody)
	require.Equal(t, 0, code)
	groupID := data["id"].(string)

	updateBody := map[string]interface{}{"name": "Updated Name"}
	statusCode, respBody = testutil.AuthPut(t, server, "/api/v1/groups/"+groupID, user.Token, updateBody)
	assert.Equal(t, http.StatusOK, statusCode)
	code, _, data = testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)
	assert.Equal(t, "Updated Name", data["name"])
}

func TestGroups_MoveClipsToGroup(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	defer server.Close()

	user := testutil.CreateTestUser(t, server)

	createBody := map[string]interface{}{"name": "Target Group"}
	statusCode, respBody := testutil.AuthPost(t, server, "/api/v1/groups", user.Token, createBody)
	require.Equal(t, http.StatusOK, statusCode)
	code, _, groupData := testutil.ParseResponse(t, respBody)
	require.Equal(t, 0, code)
	groupID := groupData["id"].(string)

	clipID := fmt.Sprintf("clip-move-%d", time.Now().UnixNano())
	createClipViaSync(t, server, user.Token, user.DeviceID, clipID, "Move me", "Content")

	moveBody := map[string]interface{}{"clip_ids": []string{clipID}}
	statusCode, respBody = testutil.AuthPost(t, server, "/api/v1/groups/"+groupID+"/move-clips", user.Token, moveBody)
	assert.Equal(t, http.StatusOK, statusCode)
	code, _, _ = testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)

	statusCode, respBody = testutil.AuthGet(t, server, "/api/v1/clips/"+clipID, user.Token)
	require.Equal(t, http.StatusOK, statusCode)
	code, _, clipData := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)
	assert.Equal(t, groupID, clipData["group_id"])
}

func TestGroups_RemoveClipsFromGroup(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	defer server.Close()

	user := testutil.CreateTestUser(t, server)

	createBody := map[string]interface{}{"name": "Remove Group"}
	statusCode, respBody := testutil.AuthPost(t, server, "/api/v1/groups", user.Token, createBody)
	require.Equal(t, http.StatusOK, statusCode)
	code, _, groupData := testutil.ParseResponse(t, respBody)
	require.Equal(t, 0, code)
	groupID := groupData["id"].(string)

	clipID := fmt.Sprintf("clip-remove-%d", time.Now().UnixNano())
	createClipViaSync(t, server, user.Token, user.DeviceID, clipID, "Remove me", "Content")

	moveBody := map[string]interface{}{"clip_ids": []string{clipID}}
	statusCode, respBody = testutil.AuthPost(t, server, "/api/v1/groups/"+groupID+"/move-clips", user.Token, moveBody)
	require.Equal(t, http.StatusOK, statusCode)

	removeBody := map[string]interface{}{"clip_ids": []string{clipID}}
	statusCode, respBody = testutil.AuthPost(t, server, "/api/v1/clips/remove-from-group", user.Token, removeBody)
	assert.Equal(t, http.StatusOK, statusCode)
	code, _, _ = testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)

	statusCode, respBody = testutil.AuthGet(t, server, "/api/v1/clips/"+clipID, user.Token)
	require.Equal(t, http.StatusOK, statusCode)
	code, _, clipData := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)
	assert.Empty(t, clipData["group_id"])
}

func TestGroups_Delete(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	defer server.Close()

	user := testutil.CreateTestUser(t, server)

	createBody := map[string]interface{}{"name": "Delete Me"}
	statusCode, respBody := testutil.AuthPost(t, server, "/api/v1/groups", user.Token, createBody)
	require.Equal(t, http.StatusOK, statusCode)
	code, _, data := testutil.ParseResponse(t, respBody)
	require.Equal(t, 0, code)
	groupID := data["id"].(string)

	statusCode, respBody = testutil.AuthDelete(t, server, "/api/v1/groups/"+groupID, user.Token)
	assert.Equal(t, http.StatusOK, statusCode)
	code, _, _ = testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)

	statusCode, _ = testutil.AuthGet(t, server, "/api/v1/groups/"+groupID, user.Token)
	assert.Equal(t, http.StatusNotFound, statusCode)
}

func TestGroups_MoveClips_InvalidGroup(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	defer server.Close()

	user := testutil.CreateTestUser(t, server)

	moveBody := map[string]interface{}{"clip_ids": []string{"clip-1"}}
	statusCode, _ := testutil.AuthPost(t, server, "/api/v1/groups/non-existent/move-clips", user.Token, moveBody)
	assert.Equal(t, http.StatusNotFound, statusCode)
}

func TestGroups_UserIsolation(t *testing.T) {
	server, _ := testutil.SetupTestServer(t)
	defer server.Close()

	admin := testutil.CreateFirstUser(t, server)
	userA := testutil.CreateUserViaAdmin(t, server, admin.Token, "groupA", "groupA@example.com", "password123")
	userB := testutil.CreateUserViaAdmin(t, server, admin.Token, "groupB", "groupB@example.com", "password123")

	createBody := map[string]interface{}{"name": "A's Group"}
	statusCode, respBody := testutil.AuthPost(t, server, "/api/v1/groups", userA.Token, createBody)
	require.Equal(t, http.StatusOK, statusCode)

	statusCode, respBody = testutil.AuthGet(t, server, "/api/v1/groups", userB.Token)
	require.Equal(t, http.StatusOK, statusCode)
	code, _, data := testutil.ParseResponse(t, respBody)
	assert.Equal(t, 0, code)
	assert.Empty(t, data)
}