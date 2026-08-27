package handler

import (
	"encoding/json"
	"errors"
	"net/http"
	"net/http/httptest"
	"os"
	"strings"
	"testing"

	"ditto-cloud-server/internal/database"
	"ditto-cloud-server/internal/response"
	"ditto-cloud-server/internal/service"

	"github.com/gin-gonic/gin"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
	"gorm.io/driver/sqlite"
	"gorm.io/gorm"
)

func TestMain(m *testing.M) {
	gin.SetMode(gin.TestMode)
	db, err := gorm.Open(sqlite.Open(":memory:"), &gorm.Config{})
	if err != nil {
		panic("failed to connect to in-memory database: " + err.Error())
	}
	database.SetDB(db)
	os.Exit(m.Run())
}

// mockClipSvc implements ClipServiceInterface for testing.
type mockClipSvc struct {
	listClipsFn          func(userID uint, page, perPage int, search, groupID, sortBy, sortOrder string) (*response.PaginatedResponse, error)
	getClipFn            func(userID uint, clipID string) (*service.ClipDetail, error)
	deleteClipFn         func(userID uint, clipID, deviceID string) error
	syncFn               func(userID uint, req *service.SyncRequest, deviceID string) (*service.SyncResponse, error)
	downloadClipFormatFn func(userID uint, clipID string, formatType int) (*service.DownloadResult, error)
	listConflictClipsFn  func(userID uint, page, perPage int) (*response.PaginatedResponse, error)
	resolveConflictClipFn func(userID uint, conflictClipID string, action string) error
	batchDeleteClipsFn   func(userID uint, clipIDs []string, deviceID string) (int64, error)
	batchMarkDontSyncFn  func(userID uint, clipIDs []string, deviceID string) (int64, error)
}

func (m *mockClipSvc) ListClips(userID uint, page, perPage int, search, groupID, sortBy, sortOrder string) (*response.PaginatedResponse, error) {
	return m.listClipsFn(userID, page, perPage, search, groupID, sortBy, sortOrder)
}
func (m *mockClipSvc) GetClip(userID uint, clipID string) (*service.ClipDetail, error) {
	return m.getClipFn(userID, clipID)
}
func (m *mockClipSvc) DeleteClip(userID uint, clipID, deviceID string) error {
	return m.deleteClipFn(userID, clipID, deviceID)
}
func (m *mockClipSvc) Sync(userID uint, req *service.SyncRequest, deviceID string) (*service.SyncResponse, error) {
	return m.syncFn(userID, req, deviceID)
}
func (m *mockClipSvc) DownloadClipFormat(userID uint, clipID string, formatType int) (*service.DownloadResult, error) {
	return m.downloadClipFormatFn(userID, clipID, formatType)
}
func (m *mockClipSvc) ListConflictClips(userID uint, page, perPage int) (*response.PaginatedResponse, error) {
	return m.listConflictClipsFn(userID, page, perPage)
}
func (m *mockClipSvc) ResolveConflictClip(userID uint, conflictClipID string, action string) error {
	return m.resolveConflictClipFn(userID, conflictClipID, action)
}
func (m *mockClipSvc) BatchDeleteClips(userID uint, clipIDs []string, deviceID string) (int64, error) {
	return m.batchDeleteClipsFn(userID, clipIDs, deviceID)
}
func (m *mockClipSvc) BatchMarkDontSync(userID uint, clipIDs []string, deviceID string) (int64, error) {
	return m.batchMarkDontSyncFn(userID, clipIDs, deviceID)
}

func setupClipTest(t *testing.T) (*gin.Context, *httptest.ResponseRecorder) {
	w := httptest.NewRecorder()
	c, _ := gin.CreateTestContext(w)
	c.Set("user_id", uint(1))
	c.Set("device_id", "test-device")
	return c, w
}

func TestListClips_Success(t *testing.T) {
	mock := &mockClipSvc{
		listClipsFn: func(userID uint, page, perPage int, search, groupID, sortBy, sortOrder string) (*response.PaginatedResponse, error) {
			assert.Equal(t, uint(1), userID)
			assert.Equal(t, 1, page)
			assert.Equal(t, 20, perPage)
			return &response.PaginatedResponse{Items: []string{}, Total: 0, Page: 1, PerPage: 20}, nil
		},
	}
	h := &ClipHandler{service: mock}
	c, w := setupClipTest(t)
	c.Request, _ = http.NewRequest(http.MethodGet, "/?page=1&per_page=20", nil)

	h.ListClips(c)

	assert.Equal(t, http.StatusOK, w.Code)
	var resp response.Response
	err := json.Unmarshal(w.Body.Bytes(), &resp)
	require.NoError(t, err)
	assert.Equal(t, 0, resp.Code)
}

func TestListClips_InvalidSortBy(t *testing.T) {
	c, w := setupClipTest(t)
	mock := &mockClipSvc{
		listClipsFn: func(userID uint, page, perPage int, search, groupID, sortBy, sortOrder string) (*response.PaginatedResponse, error) {
			return nil, service.ErrInvalidSortBy
		},
	}
	h := &ClipHandler{service: mock}
	c.Request, _ = http.NewRequest(http.MethodGet, "/?sort_by=invalid", nil)

	h.ListClips(c)

	assert.Equal(t, http.StatusBadRequest, w.Code)
	var resp response.Response
	json.Unmarshal(w.Body.Bytes(), &resp)
	assert.Equal(t, 40000, resp.Code)
}

func TestListClips_ServiceError(t *testing.T) {
	c, w := setupClipTest(t)
	mock := &mockClipSvc{
		listClipsFn: func(userID uint, page, perPage int, search, groupID, sortBy, sortOrder string) (*response.PaginatedResponse, error) {
			return nil, errors.New("db error")
		},
	}
	h := &ClipHandler{service: mock}
	c.Request, _ = http.NewRequest(http.MethodGet, "/", nil)

	h.ListClips(c)

	assert.Equal(t, http.StatusInternalServerError, w.Code)
	var resp response.Response
	json.Unmarshal(w.Body.Bytes(), &resp)
	assert.Equal(t, 50000, resp.Code)
}

func TestGetClip_Success(t *testing.T) {
	c, w := setupClipTest(t)
	mock := &mockClipSvc{
		getClipFn: func(userID uint, clipID string) (*service.ClipDetail, error) {
			assert.Equal(t, "clip-123", clipID)
			return &service.ClipDetail{ID: "clip-123", Description: "test clip"}, nil
		},
	}
	h := &ClipHandler{service: mock}
	c.Request, _ = http.NewRequest(http.MethodGet, "/clip-123", nil)
	c.Params = []gin.Param{{Key: "id", Value: "clip-123"}}

	h.GetClip(c)

	assert.Equal(t, http.StatusOK, w.Code)
	var resp response.Response
	json.Unmarshal(w.Body.Bytes(), &resp)
	assert.Equal(t, 0, resp.Code)
}

func TestGetClip_NotFound(t *testing.T) {
	c, w := setupClipTest(t)
	mock := &mockClipSvc{
		getClipFn: func(userID uint, clipID string) (*service.ClipDetail, error) {
			return nil, service.ErrClipNotFound
		},
	}
	h := &ClipHandler{service: mock}
	c.Request, _ = http.NewRequest(http.MethodGet, "/clip-404", nil)
	c.Params = []gin.Param{{Key: "id", Value: "clip-404"}}

	h.GetClip(c)

	assert.Equal(t, http.StatusNotFound, w.Code)
	var resp response.Response
	json.Unmarshal(w.Body.Bytes(), &resp)
	assert.Equal(t, 40400, resp.Code)
}

func TestDeleteClip_Success(t *testing.T) {
	c, w := setupClipTest(t)
	mock := &mockClipSvc{
		deleteClipFn: func(userID uint, clipID, deviceID string) error {
			assert.Equal(t, "clip-123", clipID)
			assert.Equal(t, "test-device", deviceID)
			return nil
		},
	}
	h := &ClipHandler{service: mock}
	c.Request, _ = http.NewRequest(http.MethodDelete, "/clip-123", nil)
	c.Params = []gin.Param{{Key: "id", Value: "clip-123"}}

	h.DeleteClip(c)

	assert.Equal(t, http.StatusOK, w.Code)
	var resp response.Response
	json.Unmarshal(w.Body.Bytes(), &resp)
	assert.Equal(t, 0, resp.Code)
	assert.Contains(t, resp.Message, "已删除")
}

func TestDeleteClip_NotFound(t *testing.T) {
	c, w := setupClipTest(t)
	mock := &mockClipSvc{
		deleteClipFn: func(userID uint, clipID, deviceID string) error {
			return service.ErrClipNotFound
		},
	}
	h := &ClipHandler{service: mock}
	c.Request, _ = http.NewRequest(http.MethodDelete, "/clip-404", nil)
	c.Params = []gin.Param{{Key: "id", Value: "clip-404"}}

	h.DeleteClip(c)

	assert.Equal(t, http.StatusNotFound, w.Code)
	var resp response.Response
	json.Unmarshal(w.Body.Bytes(), &resp)
	assert.Equal(t, 40400, resp.Code)
}

func TestSync_Success(t *testing.T) {
	c, w := setupClipTest(t)
	mock := &mockClipSvc{
		syncFn: func(userID uint, req *service.SyncRequest, deviceID string) (*service.SyncResponse, error) {
			assert.Equal(t, "test-device", req.DeviceID)
			return &service.SyncResponse{UpdatedCount: 5, SyncTime: "2024-01-01T00:00:00Z"}, nil
		},
	}
	h := &ClipHandler{service: mock}
	body := `{"device_id":"test-device","push_clips":[]}`
	c.Request, _ = http.NewRequest(http.MethodPost, "/", strings.NewReader(body))
	c.Request.Header.Set("Content-Type", "application/json")

	h.Sync(c)

	assert.Equal(t, http.StatusOK, w.Code)
	var resp response.Response
	json.Unmarshal(w.Body.Bytes(), &resp)
	assert.Equal(t, 0, resp.Code)
}

func TestSync_InvalidJSON(t *testing.T) {
	c, w := setupClipTest(t)
	mock := &mockClipSvc{}
	h := &ClipHandler{service: mock}
	c.Request, _ = http.NewRequest(http.MethodPost, "/", strings.NewReader("{invalid}"))
	c.Request.Header.Set("Content-Type", "application/json")

	h.Sync(c)

	assert.Equal(t, http.StatusBadRequest, w.Code)
}

func TestSync_DeviceIDFallback(t *testing.T) {
	c, w := setupClipTest(t)
	mock := &mockClipSvc{
		syncFn: func(userID uint, req *service.SyncRequest, deviceID string) (*service.SyncResponse, error) {
			assert.Equal(t, "test-device", req.DeviceID)
			return &service.SyncResponse{UpdatedCount: 1, SyncTime: "2024-01-01T00:00:00Z"}, nil
		},
	}
	h := &ClipHandler{service: mock}
	body := `{"push_clips":[]}`
	c.Request, _ = http.NewRequest(http.MethodPost, "/", strings.NewReader(body))
	c.Request.Header.Set("Content-Type", "application/json")

	h.Sync(c)

	assert.Equal(t, http.StatusOK, w.Code)
}

func TestSync_PushLimitExceeded(t *testing.T) {
	c, w := setupClipTest(t)
	mock := &mockClipSvc{
		syncFn: func(userID uint, req *service.SyncRequest, deviceID string) (*service.SyncResponse, error) {
			return nil, service.ErrPushLimitExceeded
		},
	}
	h := &ClipHandler{service: mock}
	body := `{"device_id":"test-device","push_clips":[]}`
	c.Request, _ = http.NewRequest(http.MethodPost, "/", strings.NewReader(body))
	c.Request.Header.Set("Content-Type", "application/json")

	h.Sync(c)

	assert.Equal(t, http.StatusRequestEntityTooLarge, w.Code)
	var resp response.Response
	json.Unmarshal(w.Body.Bytes(), &resp)
	assert.Equal(t, 41300, resp.Code)
}

func TestSync_ServiceError(t *testing.T) {
	c, w := setupClipTest(t)
	mock := &mockClipSvc{
		syncFn: func(userID uint, req *service.SyncRequest, deviceID string) (*service.SyncResponse, error) {
			return nil, errors.New("sync failed")
		},
	}
	h := &ClipHandler{service: mock}
	body := `{"device_id":"test-device","push_clips":[]}`
	c.Request, _ = http.NewRequest(http.MethodPost, "/", strings.NewReader(body))
	c.Request.Header.Set("Content-Type", "application/json")

	h.Sync(c)

	assert.Equal(t, http.StatusInternalServerError, w.Code)
}

func TestDownloadClip_Success(t *testing.T) {
	c, w := setupClipTest(t)
	mock := &mockClipSvc{
		downloadClipFormatFn: func(userID uint, clipID string, formatType int) (*service.DownloadResult, error) {
			assert.Equal(t, "clip-123", clipID)
			assert.Equal(t, 13, formatType)
			return &service.DownloadResult{Data: []byte("hello"), ContentType: "text/plain; charset=utf-16", FileName: "clip.txt"}, nil
		},
	}
	h := &ClipHandler{service: mock}
	c.Request, _ = http.NewRequest(http.MethodGet, "/clip-123/download?format_type=13", nil)
	c.Params = []gin.Param{{Key: "id", Value: "clip-123"}}

	h.DownloadClip(c)

	assert.Equal(t, http.StatusOK, w.Code)
	assert.Equal(t, "attachment; filename=clip.txt", w.Header().Get("Content-Disposition"))
	assert.Equal(t, "text/plain; charset=utf-16", w.Header().Get("Content-Type"))
	assert.Equal(t, "5", w.Header().Get("Content-Length"))
	assert.Equal(t, "hello", w.Body.String())
}

func TestDownloadClip_NotFound(t *testing.T) {
	c, w := setupClipTest(t)
	mock := &mockClipSvc{
		downloadClipFormatFn: func(userID uint, clipID string, formatType int) (*service.DownloadResult, error) {
			return nil, service.ErrClipNotFound
		},
	}
	h := &ClipHandler{service: mock}
	c.Request, _ = http.NewRequest(http.MethodGet, "/clip-404/download", nil)
	c.Params = []gin.Param{{Key: "id", Value: "clip-404"}}

	h.DownloadClip(c)

	assert.Equal(t, http.StatusNotFound, w.Code)
	var resp response.Response
	json.Unmarshal(w.Body.Bytes(), &resp)
	assert.Equal(t, 40400, resp.Code)
}

func TestDownloadClip_FormatNotFound(t *testing.T) {
	c, w := setupClipTest(t)
	mock := &mockClipSvc{
		downloadClipFormatFn: func(userID uint, clipID string, formatType int) (*service.DownloadResult, error) {
			return nil, service.ErrFormatNotFound
		},
	}
	h := &ClipHandler{service: mock}
	c.Request, _ = http.NewRequest(http.MethodGet, "/clip-123/download", nil)
	c.Params = []gin.Param{{Key: "id", Value: "clip-123"}}

	h.DownloadClip(c)

	assert.Equal(t, http.StatusNotFound, w.Code)
	var resp response.Response
	json.Unmarshal(w.Body.Bytes(), &resp)
	assert.Equal(t, 40401, resp.Code)
}

func TestGetChanges_Success(t *testing.T) {
	c, w := setupClipTest(t)
	mock := &mockClipSvc{
		syncFn: func(userID uint, req *service.SyncRequest, deviceID string) (*service.SyncResponse, error) {
			assert.Equal(t, "test-device", deviceID)
			return &service.SyncResponse{
				NewClips:   []service.ClipDetail{{ID: "clip-1"}},
				SyncTime:   "2024-01-01T00:00:00Z",
				HasMore:    false,
				DeletedIDs: []string{},
			}, nil
		},
	}
	h := &ClipHandler{service: mock}
	c.Request, _ = http.NewRequest(http.MethodGet, "/?since=2024-01-01T00:00:00Z", nil)

	h.GetChanges(c)

	assert.Equal(t, http.StatusOK, w.Code)
	var resp response.Response
	json.Unmarshal(w.Body.Bytes(), &resp)
	assert.Equal(t, 0, resp.Code)
}

func TestGetChanges_InvalidSince(t *testing.T) {
	c, w := setupClipTest(t)
	mock := &mockClipSvc{}
	h := &ClipHandler{service: mock}
	c.Request, _ = http.NewRequest(http.MethodGet, "/?since=not-a-date", nil)

	h.GetChanges(c)

	assert.Equal(t, http.StatusBadRequest, w.Code)
}

func TestGetChanges_DefaultSince(t *testing.T) {
	c, w := setupClipTest(t)
	mock := &mockClipSvc{
		syncFn: func(userID uint, req *service.SyncRequest, deviceID string) (*service.SyncResponse, error) {
			assert.Equal(t, "1970-01-01T00:00:00Z", req.Since.Format("2006-01-02T15:04:05Z"))
			return &service.SyncResponse{NewClips: []service.ClipDetail{}, SyncTime: "2024-01-01T00:00:00Z"}, nil
		},
	}
	h := &ClipHandler{service: mock}
	c.Request, _ = http.NewRequest(http.MethodGet, "/", nil)

	h.GetChanges(c)

	assert.Equal(t, http.StatusOK, w.Code)
}

func TestListConflictClips_Success(t *testing.T) {
	c, w := setupClipTest(t)
	mock := &mockClipSvc{
		listConflictClipsFn: func(userID uint, page, perPage int) (*response.PaginatedResponse, error) {
			return &response.PaginatedResponse{Items: []string{}, Total: 0, Page: 1, PerPage: 20}, nil
		},
	}
	h := &ClipHandler{service: mock}
	c.Request, _ = http.NewRequest(http.MethodGet, "/", nil)

	h.ListConflictClips(c)

	assert.Equal(t, http.StatusOK, w.Code)
	var resp response.Response
	json.Unmarshal(w.Body.Bytes(), &resp)
	assert.Equal(t, 0, resp.Code)
}

func TestResolveConflictClip_Success(t *testing.T) {
	c, w := setupClipTest(t)
	mock := &mockClipSvc{
		resolveConflictClipFn: func(userID uint, conflictClipID string, action string) error {
			assert.Equal(t, "accept", action)
			return nil
		},
	}
	h := &ClipHandler{service: mock}
	body := `{"action":"accept"}`
	c.Request, _ = http.NewRequest(http.MethodPost, "/", strings.NewReader(body))
	c.Request.Header.Set("Content-Type", "application/json")
	c.Params = []gin.Param{{Key: "id", Value: "conflict-1"}}

	h.ResolveConflictClip(c)

	assert.Equal(t, http.StatusOK, w.Code)
	var resp response.Response
	json.Unmarshal(w.Body.Bytes(), &resp)
	assert.Equal(t, 0, resp.Code)
	assert.Contains(t, resp.Message, "冲突已处理")
}

func TestResolveConflictClip_InvalidAction(t *testing.T) {
	c, w := setupClipTest(t)
	mock := &mockClipSvc{}
	h := &ClipHandler{service: mock}
	body := `{"action":"invalid"}`
	c.Request, _ = http.NewRequest(http.MethodPost, "/", strings.NewReader(body))
	c.Request.Header.Set("Content-Type", "application/json")
	c.Params = []gin.Param{{Key: "id", Value: "conflict-1"}}

	h.ResolveConflictClip(c)

	assert.Equal(t, http.StatusBadRequest, w.Code)
}

func TestResolveConflictClip_NotFound(t *testing.T) {
	c, w := setupClipTest(t)
	mock := &mockClipSvc{
		resolveConflictClipFn: func(userID uint, conflictClipID string, action string) error {
			return service.ErrConflictClipNotFound
		},
	}
	h := &ClipHandler{service: mock}
	body := `{"action":"accept"}`
	c.Request, _ = http.NewRequest(http.MethodPost, "/", strings.NewReader(body))
	c.Request.Header.Set("Content-Type", "application/json")
	c.Params = []gin.Param{{Key: "id", Value: "conflict-1"}}

	h.ResolveConflictClip(c)

	assert.Equal(t, http.StatusNotFound, w.Code)
	var resp response.Response
	json.Unmarshal(w.Body.Bytes(), &resp)
	assert.Equal(t, 40400, resp.Code)
}

func TestResolveConflictClip_MissingBody(t *testing.T) {
	c, w := setupClipTest(t)
	mock := &mockClipSvc{}
	h := &ClipHandler{service: mock}
	c.Request, _ = http.NewRequest(http.MethodPost, "/", nil)
	c.Request.Header.Set("Content-Type", "application/json")
	c.Params = []gin.Param{{Key: "id", Value: "conflict-1"}}

	h.ResolveConflictClip(c)

	assert.Equal(t, http.StatusBadRequest, w.Code)
}

func TestBatchDeleteClips_Success(t *testing.T) {
	c, w := setupClipTest(t)
	mock := &mockClipSvc{
		batchDeleteClipsFn: func(userID uint, clipIDs []string, deviceID string) (int64, error) {
			assert.Equal(t, []string{"c1", "c2"}, clipIDs)
			assert.Equal(t, "test-device", deviceID)
			return 2, nil
		},
	}
	h := &ClipHandler{service: mock}
	body := `{"ids":["c1","c2"]}`
	c.Request, _ = http.NewRequest(http.MethodPost, "/", strings.NewReader(body))
	c.Request.Header.Set("Content-Type", "application/json")

	h.BatchDeleteClips(c)

	assert.Equal(t, http.StatusOK, w.Code)
	var resp response.Response
	json.Unmarshal(w.Body.Bytes(), &resp)
	assert.Equal(t, 0, resp.Code)
	assert.Contains(t, resp.Message, "成功删除")
}

func TestBatchDeleteClips_EmptyIDs(t *testing.T) {
	c, w := setupClipTest(t)
	mock := &mockClipSvc{}
	h := &ClipHandler{service: mock}
	body := `{"ids":[]}`
	c.Request, _ = http.NewRequest(http.MethodPost, "/", strings.NewReader(body))
	c.Request.Header.Set("Content-Type", "application/json")

	h.BatchDeleteClips(c)

	assert.Equal(t, http.StatusBadRequest, w.Code)
}

func TestBatchDeleteClips_MissingBody(t *testing.T) {
	c, w := setupClipTest(t)
	mock := &mockClipSvc{}
	h := &ClipHandler{service: mock}
	c.Request, _ = http.NewRequest(http.MethodPost, "/", nil)
	c.Request.Header.Set("Content-Type", "application/json")

	h.BatchDeleteClips(c)

	assert.Equal(t, http.StatusBadRequest, w.Code)
}

func TestBatchMarkDontSync_Success(t *testing.T) {
	c, w := setupClipTest(t)
	mock := &mockClipSvc{
		batchMarkDontSyncFn: func(userID uint, clipIDs []string, deviceID string) (int64, error) {
			assert.Equal(t, []string{"c1", "c2"}, clipIDs)
			assert.Equal(t, "test-device", deviceID)
			return 2, nil
		},
	}
	h := &ClipHandler{service: mock}
	body := `{"ids":["c1","c2"]}`
	c.Request, _ = http.NewRequest(http.MethodPost, "/", strings.NewReader(body))
	c.Request.Header.Set("Content-Type", "application/json")

	h.BatchMarkDontSync(c)

	assert.Equal(t, http.StatusOK, w.Code)
	var resp response.Response
	json.Unmarshal(w.Body.Bytes(), &resp)
	assert.Equal(t, 0, resp.Code)
	assert.Contains(t, resp.Message, "成功标记")
}

func TestBatchMarkDontSync_EmptyIDs(t *testing.T) {
	c, w := setupClipTest(t)
	mock := &mockClipSvc{}
	h := &ClipHandler{service: mock}
	body := `{"ids":[]}`
	c.Request, _ = http.NewRequest(http.MethodPost, "/", strings.NewReader(body))
	c.Request.Header.Set("Content-Type", "application/json")

	h.BatchMarkDontSync(c)

	assert.Equal(t, http.StatusBadRequest, w.Code)
}
