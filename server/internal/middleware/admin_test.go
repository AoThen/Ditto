package middleware

import (
	"net/http"
	"net/http/httptest"
	"os"
	"testing"
	"time"

	"ditto-cloud-server/internal/database"
	"ditto-cloud-server/internal/model"

	"github.com/gin-gonic/gin"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

func setupAdminMiddlewareTest(t *testing.T) (normalUserID, adminUserID uint, cleanup func()) {
	t.Helper()

	tmpFile, err := os.CreateTemp("", "admin_middleware_test_*.db")
	require.NoError(t, err)
	dbPath := tmpFile.Name()
	tmpFile.Close()

	err = database.Init(dbPath, 500*time.Millisecond)
	require.NoError(t, err)

	now := time.Now()

	normalUser := model.User{
		Username:     "normaluser",
		Email:        "normal@example.com",
		PasswordHash: "hash",
		Role:         "user",
		CreatedAt:    now,
		UpdatedAt:    now,
	}
	database.DB.Create(&normalUser)

	adminUser := model.User{
		Username:     "adminuser",
		Email:        "admin@example.com",
		PasswordHash: "hash",
		Role:         "admin",
		CreatedAt:    now,
		UpdatedAt:    now,
	}
	database.DB.Create(&adminUser)

	cleanup = func() {
		database.DB = nil
		os.Remove(dbPath)
		os.Remove(dbPath + "-shm")
		os.Remove(dbPath + "-wal")
	}

	return normalUser.ID, adminUser.ID, cleanup
}

func createAdminTestRouter(setUserID bool, userID uint) *gin.Engine {
	gin.SetMode(gin.TestMode)
	router := gin.New()
	router.GET("/admin/test", func(c *gin.Context) {
		if setUserID {
			c.Set("user_id", userID)
		}
		c.Next()
	}, AdminAuth(), func(c *gin.Context) {
		role, _ := c.Get("user_role")
		c.JSON(http.StatusOK, gin.H{
			"user_role": role,
		})
	})
	return router
}

func TestAdminAuth_NoToken(t *testing.T) {
	_, _, cleanup := setupAdminMiddlewareTest(t)
	defer cleanup()

	router := createAdminTestRouter(false, 0)

	req, _ := http.NewRequest("GET", "/admin/test", nil)
	w := httptest.NewRecorder()
	router.ServeHTTP(w, req)

	assert.Equal(t, http.StatusUnauthorized, w.Code)
	assert.Contains(t, w.Body.String(), "未提供认证令牌")
}

func TestAdminAuth_ZeroUserID(t *testing.T) {
	_, _, cleanup := setupAdminMiddlewareTest(t)
	defer cleanup()

	router := createAdminTestRouter(true, 0)

	req, _ := http.NewRequest("GET", "/admin/test", nil)
	w := httptest.NewRecorder()
	router.ServeHTTP(w, req)

	assert.Equal(t, http.StatusUnauthorized, w.Code)
	assert.Contains(t, w.Body.String(), "未提供认证令牌")
}

func TestAdminAuth_UserNotFound(t *testing.T) {
	_, _, cleanup := setupAdminMiddlewareTest(t)
	defer cleanup()

	router := createAdminTestRouter(true, 999)

	req, _ := http.NewRequest("GET", "/admin/test", nil)
	w := httptest.NewRecorder()
	router.ServeHTTP(w, req)

	assert.Equal(t, http.StatusForbidden, w.Code)
	assert.Contains(t, w.Body.String(), "用户不存在")
}

func TestAdminAuth_NotAdmin(t *testing.T) {
	normalUserID, _, cleanup := setupAdminMiddlewareTest(t)
	defer cleanup()

	router := createAdminTestRouter(true, normalUserID)

	req, _ := http.NewRequest("GET", "/admin/test", nil)
	w := httptest.NewRecorder()
	router.ServeHTTP(w, req)

	assert.Equal(t, http.StatusForbidden, w.Code)
	assert.Contains(t, w.Body.String(), "需要管理员权限")
}

func TestAdminAuth_Success(t *testing.T) {
	_, adminUserID, cleanup := setupAdminMiddlewareTest(t)
	defer cleanup()

	router := createAdminTestRouter(true, adminUserID)

	req, _ := http.NewRequest("GET", "/admin/test", nil)
	w := httptest.NewRecorder()
	router.ServeHTTP(w, req)

	assert.Equal(t, http.StatusOK, w.Code)
	assert.Contains(t, w.Body.String(), `"user_role":"admin"`)
}
