package service

import (
	"errors"
	"os"
	"testing"
	"time"

	"ditto-cloud-server/internal/database"
	"ditto-cloud-server/internal/model"
	"ditto-cloud-server/pkg/crypto"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
	"gorm.io/gorm"
)

func setupUserServiceTest(t *testing.T) (*UserService, func()) {
	t.Helper()

	tmpFile, err := os.CreateTemp("", "user_service_test_*.db")
	require.NoError(t, err)
	dbPath := tmpFile.Name()
	tmpFile.Close()

	err = database.Init(dbPath, 500*time.Millisecond)
	require.NoError(t, err)

	cleanup := func() {
		database.DB = nil
		os.Remove(dbPath)
		os.Remove(dbPath + "-shm")
		os.Remove(dbPath + "-wal")
	}

	return NewUserService(), cleanup
}

func seedUserRow(t *testing.T, username, email, role string) model.User {
	t.Helper()
	hash, err := crypto.HashPassword("password-" + username)
	require.NoError(t, err)
	user := model.User{
		Username:     username,
		Email:        email,
		PasswordHash: hash,
		Role:         role,
		IsActive:     true,
	}
	require.NoError(t, database.DB.Create(&user).Error)
	return user
}

func seedDeviceRow(t *testing.T, userID uint, deviceID string, tokenVersion int) model.Device {
	t.Helper()
	device := model.Device{
		ID:           deviceID,
		UserID:       userID,
		DeviceName:   deviceID,
		TokenVersion: tokenVersion,
	}
	require.NoError(t, database.DB.Create(&device).Error)
	return device
}

func TestUserService_CreateUser_Success(t *testing.T) {
	svc, cleanup := setupUserServiceTest(t)
	defer cleanup()

	user, err := svc.CreateUser("alice", "alice@example.com", "secret123")

	require.NoError(t, err)
	assert.NotZero(t, user.ID)
	assert.Equal(t, "alice", user.Username)
	assert.Equal(t, "alice@example.com", user.Email)
	assert.Equal(t, "user", user.Role)
	assert.True(t, user.IsActive)
	assert.NotEqual(t, "secret123", user.PasswordHash)
	assert.True(t, crypto.CheckPasswordHash("secret123", user.PasswordHash))

	var count int64
	database.DB.Model(&model.User{}).Count(&count)
	assert.Equal(t, int64(1), count)
}

func TestUserService_CreateUser_DuplicateUsername(t *testing.T) {
	svc, cleanup := setupUserServiceTest(t)
	defer cleanup()

	_, err := svc.CreateUser("bob", "bob@example.com", "password")
	require.NoError(t, err)

	_, err = svc.CreateUser("bob", "other@example.com", "password")
	require.Error(t, err)
	assert.Contains(t, err.Error(), "用户名已存在")
}

func TestUserService_CreateUser_DuplicateEmail(t *testing.T) {
	svc, cleanup := setupUserServiceTest(t)
	defer cleanup()

	_, err := svc.CreateUser("carl", "carl@example.com", "password")
	require.NoError(t, err)

	_, err = svc.CreateUser("dave", "carl@example.com", "password")
	require.Error(t, err)
	assert.Contains(t, err.Error(), "邮箱已被注册")
}

func TestUserService_ListUsers_Pagination(t *testing.T) {
	svc, cleanup := setupUserServiceTest(t)
	defer cleanup()

	seedUserRow(t, "user1", "user1@example.com", "user")
	seedUserRow(t, "user2", "user2@example.com", "user")
	seedUserRow(t, "user3", "user3@example.com", "user")

	users, total, err := svc.ListUsers("", 1, 2)
	require.NoError(t, err)
	assert.Equal(t, int64(3), total)
	assert.Len(t, users, 2)
	assert.Equal(t, "user3", users[0].Username)

	users, total, err = svc.ListUsers("", 2, 2)
	require.NoError(t, err)
	assert.Equal(t, int64(3), total)
	assert.Len(t, users, 1)
	assert.Equal(t, "user1", users[0].Username)
}

func TestUserService_ListUsers_Search(t *testing.T) {
	svc, cleanup := setupUserServiceTest(t)
	defer cleanup()

	seedUserRow(t, "100%user", "pct@example.com", "user")
	seedUserRow(t, "100Xuser", "exx@example.com", "user")
	seedUserRow(t, "plainuser", "plain@test.org", "user")

	users, total, err := svc.ListUsers("100%user", 1, 10)
	require.NoError(t, err)
	assert.Equal(t, int64(1), total, "% in search term must be escaped, not treated as wildcard")
	assert.Len(t, users, 1)
	assert.Equal(t, "100%user", users[0].Username)

	users, total, err = svc.ListUsers("example", 1, 10)
	require.NoError(t, err)
	assert.Equal(t, int64(2), total, "search must match by email too")

	users, total, err = svc.ListUsers("nomatch", 1, 10)
	require.NoError(t, err)
	assert.Equal(t, int64(0), total)
	assert.Empty(t, users)
}

func TestUserService_GetUser(t *testing.T) {
	svc, cleanup := setupUserServiceTest(t)
	defer cleanup()

	seeded := seedUserRow(t, "eve", "eve@example.com", "user")

	user, err := svc.GetUser(seeded.ID)
	require.NoError(t, err)
	assert.Equal(t, "eve", user.Username)
	assert.Equal(t, "eve@example.com", user.Email)

	_, err = svc.GetUser(99999)
	require.Error(t, err)
	assert.True(t, errors.Is(err, gorm.ErrRecordNotFound))
}

func TestUserService_CheckUserExists(t *testing.T) {
	svc, cleanup := setupUserServiceTest(t)
	defer cleanup()

	seeded := seedUserRow(t, "frank", "frank@example.com", "user")

	assert.NoError(t, svc.CheckUserExists(seeded.ID))
	assert.Error(t, svc.CheckUserExists(99999))
}

func TestUserService_GetDeviceCount(t *testing.T) {
	svc, cleanup := setupUserServiceTest(t)
	defer cleanup()

	seeded := seedUserRow(t, "grace", "grace@example.com", "user")
	seedDeviceRow(t, seeded.ID, "device-a", 0)
	seedDeviceRow(t, seeded.ID, "device-b", 0)

	count, err := svc.GetDeviceCount(seeded.ID)
	require.NoError(t, err)
	assert.Equal(t, int64(2), count)
}

func TestUserService_CountUsersByRole(t *testing.T) {
	svc, cleanup := setupUserServiceTest(t)
	defer cleanup()

	seedUserRow(t, "admin1", "admin1@example.com", "admin")
	seedUserRow(t, "user1", "user1@example.com", "user")
	seedUserRow(t, "user2", "user2@example.com", "user")

	adminCount, err := svc.CountUsersByRole("admin")
	require.NoError(t, err)
	assert.Equal(t, int64(1), adminCount)

	userCount, err := svc.CountUsersByRole("user")
	require.NoError(t, err)
	assert.Equal(t, int64(2), userCount)
}

func TestUserService_CheckEmailTakenByOther(t *testing.T) {
	svc, cleanup := setupUserServiceTest(t)
	defer cleanup()

	u1 := seedUserRow(t, "henry", "henry@example.com", "user")
	u2 := seedUserRow(t, "iris", "iris@example.com", "user")

	err := svc.CheckEmailTakenByOther("henry@example.com", u2.ID)
	require.Error(t, err)
	assert.Contains(t, err.Error(), "邮箱已被其他用户使用")

	assert.NoError(t, svc.CheckEmailTakenByOther("henry@example.com", u1.ID))
	assert.NoError(t, svc.CheckEmailTakenByOther("free@example.com", u1.ID))
}

func TestUserService_UpdateUser_FieldWhitelist(t *testing.T) {
	svc, cleanup := setupUserServiceTest(t)
	defer cleanup()

	seeded := seedUserRow(t, "jack", "jack@example.com", "user")
	originalHash := seeded.PasswordHash

	err := svc.UpdateUser(seeded.ID, map[string]interface{}{
		"role":          "admin",
		"email":         "jack+new@example.com",
		"is_active":     false,
		"password_hash": "hacked",
		"username":      "hacked-name",
		"bogus":         "x",
	})
	require.NoError(t, err)

	updated, err := svc.GetUser(seeded.ID)
	require.NoError(t, err)
	assert.Equal(t, "admin", updated.Role)
	assert.Equal(t, "jack+new@example.com", updated.Email)
	assert.False(t, updated.IsActive)
	assert.Equal(t, "jack", updated.Username, "username is not an allowed update field")
	assert.Equal(t, originalHash, updated.PasswordHash, "password_hash must be filtered out")
}

func TestUserService_UpdateUser_LastAdminDemote(t *testing.T) {
	svc, cleanup := setupUserServiceTest(t)
	defer cleanup()

	admin := seedUserRow(t, "lastadmin", "lastadmin@example.com", "admin")

	err := svc.UpdateUser(admin.ID, map[string]interface{}{"role": "user"})
	require.Error(t, err)
	assert.Contains(t, err.Error(), "无法将最后一个管理员降级为普通用户")

	seedUserRow(t, "secondadmin", "secondadmin@example.com", "admin")
	assert.NoError(t, svc.UpdateUser(admin.ID, map[string]interface{}{"role": "user"}))
}

func TestUserService_UpdateUser_DisableRevokesDeviceTokens(t *testing.T) {
	svc, cleanup := setupUserServiceTest(t)
	defer cleanup()

	seeded := seedUserRow(t, "kate", "kate@example.com", "user")
	seedDeviceRow(t, seeded.ID, "device-a", 0)
	seedDeviceRow(t, seeded.ID, "device-b", 3)

	require.NoError(t, svc.UpdateUser(seeded.ID, map[string]interface{}{"is_active": false}))

	var devices []model.Device
	require.NoError(t, database.DB.Where("user_id = ?", seeded.ID).Order("id").Find(&devices).Error)
	require.Len(t, devices, 2)
	assert.Equal(t, 1, devices[0].TokenVersion, "token_version must be incremented on disable")
	assert.Equal(t, 4, devices[1].TokenVersion, "existing token_version must be preserved and incremented")
}

func TestUserService_DeleteUser_Cascade(t *testing.T) {
	svc, cleanup := setupUserServiceTest(t)
	defer cleanup()

	seeded := seedUserRow(t, "leo", "leo@example.com", "user")
	seedDeviceRow(t, seeded.ID, "device-a", 0)

	group := model.Group{ID: "group-1", UserID: seeded.ID, Name: "root"}
	require.NoError(t, database.DB.Create(&group).Error)

	clip := model.Clip{ID: "clip-1", UserID: seeded.ID, DeviceID: "device-a", Description: "hello"}
	require.NoError(t, database.DB.Create(&clip).Error)
	format := model.ClipFormat{ClipID: clip.ID, FormatType: 1, Data: []byte("data")}
	require.NoError(t, database.DB.Create(&format).Error)

	syncLog := model.SyncLog{UserID: seeded.ID, DeviceID: "device-a", Action: "push", Status: "success", SyncedAt: time.Now()}
	require.NoError(t, database.DB.Create(&syncLog).Error)

	settings := model.EncryptionSettings{
		UserID:           seeded.ID,
		Salt:             []byte("salt"),
		WrappedDEK:       []byte("dek"),
		VerificationHash: []byte("vh"),
	}
	require.NoError(t, database.DB.Create(&settings).Error)

	require.NoError(t, svc.DeleteUser(seeded.ID))

	assert.Equal(t, int64(0), countRows(t, &model.User{}, "id", seeded.ID))
	assert.Equal(t, int64(0), countRows(t, &model.Device{}, "user_id", seeded.ID))
	assert.Equal(t, int64(0), countRows(t, &model.Group{}, "user_id", seeded.ID))
	assert.Equal(t, int64(0), countRows(t, &model.Clip{}, "user_id", seeded.ID), "clips must be removed for the deleted user")
	assert.Equal(t, int64(0), countRows(t, &model.ClipFormat{}, "clip_id", clip.ID))
	assert.Equal(t, int64(0), countRows(t, &model.SyncLog{}, "user_id", seeded.ID))
	assert.Equal(t, int64(0), countRows(t, &model.EncryptionSettings{}, "user_id", seeded.ID))
}

func TestUserService_DeleteUser_LastAdmin(t *testing.T) {
	svc, cleanup := setupUserServiceTest(t)
	defer cleanup()

	admin := seedUserRow(t, "solo", "solo@example.com", "admin")

	err := svc.DeleteUser(admin.ID)
	require.Error(t, err)
	assert.Contains(t, err.Error(), "无法删除最后一个管理员账号")

	_, getErr := svc.GetUser(admin.ID)
	assert.NoError(t, getErr, "last admin must not be deleted")
}

func TestUserService_DeleteUser_NotFound(t *testing.T) {
	svc, cleanup := setupUserServiceTest(t)
	defer cleanup()

	err := svc.DeleteUser(99999)
	require.Error(t, err)
	assert.True(t, errors.Is(err, gorm.ErrRecordNotFound))
}

func TestUserService_ResetPassword(t *testing.T) {
	svc, cleanup := setupUserServiceTest(t)
	defer cleanup()

	hash, err := crypto.HashPassword("oldpass")
	require.NoError(t, err)
	seeded := model.User{Username: "mona", Email: "mona@example.com", PasswordHash: hash, Role: "user", IsActive: true}
	require.NoError(t, database.DB.Create(&seeded).Error)
	seedDeviceRow(t, seeded.ID, "device-a", 2)

	require.NoError(t, svc.ResetPassword(seeded.ID, "newpass123"))

	updated, err := svc.GetUser(seeded.ID)
	require.NoError(t, err)
	assert.True(t, crypto.CheckPasswordHash("newpass123", updated.PasswordHash))
	assert.False(t, crypto.CheckPasswordHash("oldpass", updated.PasswordHash))

	var device model.Device
	require.NoError(t, database.DB.First(&device, "id = ?", "device-a").Error)
	assert.Equal(t, 3, device.TokenVersion, "reset must revoke device tokens")
}

func countRows(t *testing.T, dest interface{}, column string, value interface{}) int64 {
	t.Helper()
	var count int64
	require.NoError(t, database.DB.Model(dest).Where(column+" = ?", value).Count(&count).Error)
	return count
}
