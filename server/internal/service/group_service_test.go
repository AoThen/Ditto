package service

import (
	"os"
	"testing"

	"ditto-cloud-server/internal/database"
	"ditto-cloud-server/internal/model"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// setupGroupServiceTest creates an isolated test environment for GroupService tests
func setupGroupServiceTest(t *testing.T) (*GroupService, uint, func()) {
	t.Helper()

	// Create temp database file
	tmpFile, err := os.CreateTemp("", "group_service_test_*.db")
	require.NoError(t, err)
	dbPath := tmpFile.Name()
	tmpFile.Close()

	// Initialize database
	err = database.Init(dbPath)
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
	svc := NewGroupService()

	cleanup := func() {
		database.DB = nil
		os.Remove(dbPath)
		os.Remove(dbPath + "-shm")
		os.Remove(dbPath + "-wal")
	}

	return svc, user.ID, cleanup
}

func TestNewGroupService(t *testing.T) {
	svc := NewGroupService()
	assert.NotNil(t, svc)
}

func TestGroupService_ListGroups_Success(t *testing.T) {
	svc, userID, cleanup := setupGroupServiceTest(t)
	defer cleanup()

	// Create test groups
	group1 := model.Group{
		ID:        "group-1",
		UserID:    userID,
		Name:      "Group 1",
		ClipOrder: 1.0,
	}
	group2 := model.Group{
		ID:        "group-2",
		UserID:    userID,
		Name:      "Group 2",
		ClipOrder: 2.0,
	}
	require.NoError(t, database.DB.Create(&group1).Error)
	require.NoError(t, database.DB.Create(&group2).Error)

	// List groups
	result, err := svc.ListGroups(userID, 1, 20)

	assert.NoError(t, err)
	groups := result.Items.([]GroupListItem)
	assert.Len(t, groups, 2)
	assert.Equal(t, "group-1", groups[0].ID)
	assert.Equal(t, "group-2", groups[1].ID)
}

func TestGroupService_ListGroups_Empty(t *testing.T) {
	svc, userID, cleanup := setupGroupServiceTest(t)
	defer cleanup()

	// List groups for user with no groups
	result, err := svc.ListGroups(userID, 1, 20)

	assert.NoError(t, err)
	assert.Empty(t, result.Items)
}

func TestGroupService_GetGroup_Success(t *testing.T) {
	svc, userID, cleanup := setupGroupServiceTest(t)
	defer cleanup()

	// Create a test group
	group := model.Group{
		ID:          "group-1",
		UserID:      userID,
		Name:        "Test Group",
		Description: "Test Description",
		ClipOrder:   1.0,
	}
	require.NoError(t, database.DB.Create(&group).Error)

	// Get the group
	detail, err := svc.GetGroup(userID, group.ID)

	assert.NoError(t, err)
	assert.NotNil(t, detail)
	assert.Equal(t, "group-1", detail.ID)
	assert.Equal(t, "Test Group", detail.Name)
	assert.Equal(t, "Test Description", detail.Description)
	assert.Equal(t, int64(0), detail.ClipCount)
}

func TestGroupService_GetGroup_NotFound(t *testing.T) {
	svc, userID, cleanup := setupGroupServiceTest(t)
	defer cleanup()

	// Get non-existent group
	detail, err := svc.GetGroup(userID, "non-existent")

	assert.Error(t, err)
	assert.Nil(t, detail)
	assert.Equal(t, "分组不存在", err.Error())
}

func TestGroupService_GetGroup_WithChildren(t *testing.T) {
	svc, userID, cleanup := setupGroupServiceTest(t)
	defer cleanup()

	// Create parent and child groups
	parent := model.Group{
		ID:        "parent-group",
		UserID:    userID,
		Name:      "Parent",
		ClipOrder: 1.0,
	}
	child := model.Group{
		ID:        "child-group",
		UserID:    userID,
		Name:      "Child",
		ParentID:  &parent.ID,
		ClipOrder: 1.0,
	}
	require.NoError(t, database.DB.Create(&parent).Error)
	require.NoError(t, database.DB.Create(&child).Error)

	// Get the parent group
	detail, err := svc.GetGroup(userID, parent.ID)

	assert.NoError(t, err)
	assert.NotNil(t, detail)
	assert.Len(t, detail.Children, 1)
	assert.Equal(t, "child-group", detail.Children[0].ID)
}

func TestGroupService_CreateGroup_Success(t *testing.T) {
	svc, userID, cleanup := setupGroupServiceTest(t)
	defer cleanup()

	req := &CreateGroupRequest{
		Name:        "New Group",
		Description: "Test Description",
		ClipOrder:   1.0,
	}

	result, err := svc.CreateGroup(userID, req)

	assert.NoError(t, err)
	assert.NotNil(t, result)
	assert.NotEmpty(t, result.ID)
	assert.Equal(t, "New Group", result.Name)
	assert.Equal(t, "Test Description", result.Description)
	assert.Equal(t, int64(0), result.ClipCount)
}

func TestGroupService_CreateGroup_WithParent(t *testing.T) {
	svc, userID, cleanup := setupGroupServiceTest(t)
	defer cleanup()

	// Create parent group
	parent := model.Group{
		ID:        "parent-group",
		UserID:    userID,
		Name:      "Parent",
		ClipOrder: 1.0,
	}
	require.NoError(t, database.DB.Create(&parent).Error)

	// Create child group
	req := &CreateGroupRequest{
		Name:      "Child Group",
		ParentID:  &parent.ID,
		ClipOrder: 1.0,
	}

	result, err := svc.CreateGroup(userID, req)

	assert.NoError(t, err)
	assert.NotNil(t, result)
	assert.Equal(t, parent.ID, result.ParentID)
}

func TestGroupService_CreateGroup_InvalidParent(t *testing.T) {
	svc, userID, cleanup := setupGroupServiceTest(t)
	defer cleanup()

	invalidParentID := "non-existent-parent"
	req := &CreateGroupRequest{
		Name:     "Child Group",
		ParentID: &invalidParentID,
	}

	result, err := svc.CreateGroup(userID, req)

	assert.Error(t, err)
	assert.Nil(t, result)
	assert.Equal(t, "父分组不存在", err.Error())
}

func TestGroupService_UpdateGroup_Success(t *testing.T) {
	svc, userID, cleanup := setupGroupServiceTest(t)
	defer cleanup()

	// Create a group
	group := model.Group{
		ID:          "group-to-update",
		UserID:      userID,
		Name:        "Original Name",
		Description: "Original Description",
		ClipOrder:   1.0,
	}
	require.NoError(t, database.DB.Create(&group).Error)

	// Update the group
	req := &UpdateGroupRequest{
		Name:        "Updated Name",
		Description: "Updated Description",
		ClipOrder:   2.0,
	}

	result, err := svc.UpdateGroup(userID, group.ID, req)

	assert.NoError(t, err)
	assert.NotNil(t, result)
	assert.Equal(t, "Updated Name", result.Name)
	assert.Equal(t, "Updated Description", result.Description)
	assert.Equal(t, 2.0, result.ClipOrder)
}

func TestGroupService_UpdateGroup_NotFound(t *testing.T) {
	svc, userID, cleanup := setupGroupServiceTest(t)
	defer cleanup()

	req := &UpdateGroupRequest{
		Name: "Updated Name",
	}

	result, err := svc.UpdateGroup(userID, "non-existent", req)

	assert.Error(t, err)
	assert.Nil(t, result)
	assert.Equal(t, "分组不存在", err.Error())
}

func TestGroupService_UpdateGroup_SelfParent(t *testing.T) {
	svc, userID, cleanup := setupGroupServiceTest(t)
	defer cleanup()

	// Create a group
	group := model.Group{
		ID:        "group-1",
		UserID:    userID,
		Name:      "Test Group",
		ClipOrder: 1.0,
	}
	require.NoError(t, database.DB.Create(&group).Error)

	// Try to set parent to self
	req := &UpdateGroupRequest{
		ParentID: &group.ID,
	}

	result, err := svc.UpdateGroup(userID, group.ID, req)

	assert.Error(t, err)
	assert.Nil(t, result)
	assert.Equal(t, "不能将分组设为自身", err.Error())
}

func TestGroupService_DeleteGroup_Success(t *testing.T) {
	svc, userID, cleanup := setupGroupServiceTest(t)
	defer cleanup()

	// Create a group
	group := model.Group{
		ID:        "group-to-delete",
		UserID:    userID,
		Name:      "Test Group",
		ClipOrder: 1.0,
	}
	require.NoError(t, database.DB.Create(&group).Error)

	// Delete the group
	err := svc.DeleteGroup(userID, group.ID)
	assert.NoError(t, err)

	// Verify group was deleted
	var count int64
	database.DB.Model(&model.Group{}).Where("id = ?", group.ID).Count(&count)
	assert.Equal(t, int64(0), count)
}

func TestGroupService_DeleteGroup_NotFound(t *testing.T) {
	svc, userID, cleanup := setupGroupServiceTest(t)
	defer cleanup()

	err := svc.DeleteGroup(userID, "non-existent")
	assert.Error(t, err)
	assert.Equal(t, "分组不存在", err.Error())
}

func TestGroupService_DeleteGroup_WithClips(t *testing.T) {
	svc, userID, cleanup := setupGroupServiceTest(t)
	defer cleanup()

	// Create a device for the clips
	device := model.Device{
		ID:         "test-device",
		UserID:     userID,
		DeviceName: "Test Device",
	}
	require.NoError(t, database.DB.Create(&device).Error)

	// Create a group
	group := model.Group{
		ID:        "group-with-clips",
		UserID:    userID,
		Name:      "Test Group",
		ClipOrder: 1.0,
	}
	require.NoError(t, database.DB.Create(&group).Error)

	// Create clips in the group
	clip := model.Clip{
		ID:          "clip-1",
		UserID:      userID,
		DeviceID:    device.ID,
		GroupID:     group.ID,
		Description: "Test Clip",
		CRC:         12345,
	}
	require.NoError(t, database.DB.Create(&clip).Error)

	// Delete the group
	err := svc.DeleteGroup(userID, group.ID)
	assert.NoError(t, err)

	// Verify group was deleted
	var groupCount int64
	database.DB.Model(&model.Group{}).Where("id = ?", group.ID).Count(&groupCount)
	assert.Equal(t, int64(0), groupCount)

	// Verify clip still exists but group_id is empty
	var updatedClip model.Clip
	require.NoError(t, database.DB.First(&updatedClip, "id = ?", clip.ID).Error)
	assert.Empty(t, updatedClip.GroupID)
}

func TestGroupService_DeleteGroup_WithChildren(t *testing.T) {
	svc, userID, cleanup := setupGroupServiceTest(t)
	defer cleanup()

	// Create parent and child groups
	parent := model.Group{
		ID:        "parent-group",
		UserID:    userID,
		Name:      "Parent",
		ClipOrder: 1.0,
	}
	child := model.Group{
		ID:        "child-group",
		UserID:    userID,
		Name:      "Child",
		ParentID:  &parent.ID,
		ClipOrder: 1.0,
	}
	require.NoError(t, database.DB.Create(&parent).Error)
	require.NoError(t, database.DB.Create(&child).Error)

	// Delete parent group
	err := svc.DeleteGroup(userID, parent.ID)
	assert.NoError(t, err)

	// Verify both groups were deleted
	var parentCount, childCount int64
	database.DB.Model(&model.Group{}).Where("id = ?", parent.ID).Count(&parentCount)
	database.DB.Model(&model.Group{}).Where("id = ?", child.ID).Count(&childCount)
	assert.Equal(t, int64(0), parentCount)
	assert.Equal(t, int64(0), childCount)
}

func TestGroupService_MoveClipsToGroup_Success(t *testing.T) {
	svc, userID, cleanup := setupGroupServiceTest(t)
	defer cleanup()

	// Create a device for the clips
	device := model.Device{
		ID:         "test-device",
		UserID:     userID,
		DeviceName: "Test Device",
	}
	require.NoError(t, database.DB.Create(&device).Error)

	// Create a group
	group := model.Group{
		ID:        "target-group",
		UserID:    userID,
		Name:      "Target Group",
		ClipOrder: 1.0,
	}
	require.NoError(t, database.DB.Create(&group).Error)

	// Create clips
	clip1 := model.Clip{
		ID:          "clip-1",
		UserID:      userID,
		DeviceID:    device.ID,
		Description: "Clip 1",
		CRC:         12345,
	}
	clip2 := model.Clip{
		ID:          "clip-2",
		UserID:      userID,
		DeviceID:    device.ID,
		Description: "Clip 2",
		CRC:         12346,
	}
	require.NoError(t, database.DB.Create(&clip1).Error)
	require.NoError(t, database.DB.Create(&clip2).Error)

	// Move clips to group
	err := svc.MoveClipsToGroup(userID, group.ID, []string{clip1.ID, clip2.ID})
	assert.NoError(t, err)

	// Verify clips were moved
	var updatedClip1, updatedClip2 model.Clip
	require.NoError(t, database.DB.First(&updatedClip1, "id = ?", clip1.ID).Error)
	require.NoError(t, database.DB.First(&updatedClip2, "id = ?", clip2.ID).Error)
	assert.Equal(t, group.ID, updatedClip1.GroupID)
	assert.Equal(t, group.ID, updatedClip2.GroupID)
}

func TestGroupService_MoveClipsToGroup_InvalidGroup(t *testing.T) {
	svc, userID, cleanup := setupGroupServiceTest(t)
	defer cleanup()

	err := svc.MoveClipsToGroup(userID, "non-existent", []string{"clip-1"})
	assert.Error(t, err)
	assert.Equal(t, "分组不存在", err.Error())
}

func TestGroupService_RemoveClipsFromGroup_Success(t *testing.T) {
	svc, userID, cleanup := setupGroupServiceTest(t)
	defer cleanup()

	// Create a device for the clips
	device := model.Device{
		ID:         "test-device",
		UserID:     userID,
		DeviceName: "Test Device",
	}
	require.NoError(t, database.DB.Create(&device).Error)

	// Create a group
	group := model.Group{
		ID:        "test-group",
		UserID:    userID,
		Name:      "Test Group",
		ClipOrder: 1.0,
	}
	require.NoError(t, database.DB.Create(&group).Error)

	// Create clip in the group
	clip := model.Clip{
		ID:          "clip-1",
		UserID:      userID,
		DeviceID:    device.ID,
		GroupID:     group.ID,
		Description: "Test Clip",
		CRC:         12345,
	}
	require.NoError(t, database.DB.Create(&clip).Error)

	// Remove clip from group
	err := svc.RemoveClipsFromGroup(userID, []string{clip.ID})
	assert.NoError(t, err)

	// Verify clip's group_id is empty
	var updatedClip model.Clip
	require.NoError(t, database.DB.First(&updatedClip, "id = ?", clip.ID).Error)
	assert.Empty(t, updatedClip.GroupID)
}
