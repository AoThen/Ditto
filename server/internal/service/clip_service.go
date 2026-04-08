package service

import (
	"encoding/base64"
	"errors"
	"time"

	"ditto-cloud-server/internal/database"
	"ditto-cloud-server/internal/model"
	"ditto-cloud-server/internal/response"

	"gorm.io/gorm"
)

// Broadcaster defines the interface for WebSocket broadcast.
type Broadcaster interface {
	BroadcastToOthers(userID int64, excludeConn interface{}, msgType string, data map[string]interface{})
}

type ClipService struct {
	broadcaster Broadcaster
}

func NewClipService(broadcaster Broadcaster) *ClipService {
	return &ClipService{broadcaster: broadcaster}
}

// ClipFormatMeta represents format metadata (without actual data) for list responses
type ClipFormatMeta struct {
	FormatType int `json:"format_type"`
	DataSize   int `json:"data_size"`
}

// ClipListItem represents a clip with format metadata (no actual data)
type ClipListItem struct {
	ID          string          `json:"id"`
	Description string          `json:"description"`
	CRC         int64           `json:"crc"`
	CreatedAt   string          `json:"created_at"`
	UpdatedAt   string          `json:"updated_at"`
	GroupID     string          `json:"group_id"`
	ShortCut    int             `json:"short_cut"`
	PasteCount  int             `json:"paste_count"`
	Formats     []ClipFormatMeta `json:"formats"`
}

// ClipDetail represents a full clip with format data (base64-encoded)
type ClipDetail struct {
	ID          string         `json:"id"`
	Description string         `json:"description"`
	CRC         int64          `json:"crc"`
	CreatedAt   string         `json:"created_at"`
	UpdatedAt   string         `json:"updated_at"`
	GroupID     string         `json:"group_id"`
	ShortCut    int            `json:"short_cut"`
	PasteCount  int            `json:"paste_count"`
	Formats     []ClipFormatFull `json:"formats"`
}

// ClipFormatFull represents format with base64-encoded data
type ClipFormatFull struct {
	FormatType int    `json:"format_type"`
	Data       string `json:"data"`
	DataSize   int    `json:"data_size"`
}

// ListClips retrieves clips for a user with pagination and optional filters
func (s *ClipService) ListClips(userID uint, page, perPage int, search, groupID string) (*response.PaginatedResponse, error) {
	if page < 1 {
		page = 1
	}
	if perPage < 1 {
		perPage = 20
	}
	if perPage > 100 {
		perPage = 100
	}

	query := database.DB.Model(&model.Clip{}).Where("user_id = ?", userID)

	if search != "" {
		query = query.Where("description LIKE ?", "%"+search+"%")
	}
	if groupID != "" {
		query = query.Where("group_id = ?", groupID)
	}

	var total int64
	if err := query.Count(&total).Error; err != nil {
		return nil, err
	}

	var clips []model.Clip
	if err := query.Order("updated_at DESC").Offset((page - 1) * perPage).Limit(perPage).Find(&clips).Error; err != nil {
		return nil, err
	}

	// Build response items with format metadata
	items := make([]ClipListItem, 0, len(clips))
	for _, clip := range clips {
		var formats []model.ClipFormat
		if err := database.DB.Where("clip_id = ?", clip.ID).Find(&formats).Error; err != nil {
			return nil, err
		}

		formatMetas := make([]ClipFormatMeta, 0, len(formats))
		for _, f := range formats {
			formatMetas = append(formatMetas, ClipFormatMeta{
				FormatType: f.FormatType,
				DataSize:   len(f.Data),
			})
		}

		items = append(items, ClipListItem{
			ID:          clip.ID,
			Description: clip.Description,
			CRC:         clip.CRC,
			CreatedAt:   clip.CreatedAt.UTC().Format(time.RFC3339),
			UpdatedAt:   clip.UpdatedAt.UTC().Format(time.RFC3339),
			GroupID:     clip.GroupID,
			ShortCut:    clip.ShortCut,
			PasteCount:  clip.PasteCount,
			Formats:     formatMetas,
		})
	}

	return &response.PaginatedResponse{
		Items:   items,
		Total:   total,
		Page:    page,
		PerPage: perPage,
	}, nil
}

// GetClip retrieves a single clip with full format data
func (s *ClipService) GetClip(userID uint, clipID string) (*ClipDetail, error) {
	var clip model.Clip
	if err := database.DB.Where("id = ? AND user_id = ?", clipID, userID).First(&clip).Error; err != nil {
		if errors.Is(err, gorm.ErrRecordNotFound) {
			return nil, errors.New("剪贴板不存在")
		}
		return nil, err
	}

	var formats []model.ClipFormat
	if err := database.DB.Where("clip_id = ?", clipID).Find(&formats).Error; err != nil {
		return nil, err
	}

	formatFulls := make([]ClipFormatFull, 0, len(formats))
	for _, f := range formats {
		formatFulls = append(formatFulls, ClipFormatFull{
			FormatType: f.FormatType,
			Data:       base64.StdEncoding.EncodeToString(f.Data),
			DataSize:   len(f.Data),
		})
	}

	return &ClipDetail{
		ID:          clip.ID,
		Description: clip.Description,
		CRC:         clip.CRC,
		CreatedAt:   clip.CreatedAt.UTC().Format(time.RFC3339),
		UpdatedAt:   clip.UpdatedAt.UTC().Format(time.RFC3339),
		GroupID:     clip.GroupID,
		ShortCut:    clip.ShortCut,
		PasteCount:  clip.PasteCount,
		Formats:     formatFulls,
	}, nil
}

// DownloadClipFormat retrieves raw binary data for a specific format of a clip
type DownloadResult struct {
	Data        []byte
	FormatType  int
	ContentType string
	FileName    string
}

func (s *ClipService) DownloadClipFormat(userID uint, clipID string, formatType int) (*DownloadResult, error) {
	var clip model.Clip
	if err := database.DB.Where("id = ? AND user_id = ?", clipID, userID).First(&clip).Error; err != nil {
		if errors.Is(err, gorm.ErrRecordNotFound) {
			return nil, errors.New("剪贴板不存在")
		}
		return nil, err
	}

	var format model.ClipFormat
	if err := database.DB.Where("clip_id = ? AND format_type = ?", clipID, formatType).First(&format).Error; err != nil {
		if errors.Is(err, gorm.ErrRecordNotFound) {
			return nil, errors.New("指定格式不存在")
		}
		return nil, err
	}

	// Determine content type and file extension
	contentType, fileName := getContentTypeForFormat(formatType)

	return &DownloadResult{
		Data:        format.Data,
		FormatType:  format.FormatType,
		ContentType: contentType,
		FileName:    fileName,
	}, nil
}

// getContentTypeForFormat returns appropriate Content-Type and suggested filename
func getContentTypeForFormat(formatType int) (string, string) {
	switch formatType {
	case 1, 7: // CF_TEXT, CF_OEMTEXT
		return "text/plain", "clip.txt"
	case 13: // CF_UNICODETEXT
		return "text/plain; charset=utf-16", "clip.txt"
	case 49: // HTML
		return "text/html", "clip.html"
	case 8, 17, 50: // CF_DIB, CF_DIBV5, custom image
		return "image/png", "clip.png"
	case 15: // CF_HDROP (file paths)
		return "text/plain", "file_paths.txt"
	default:
		return "application/octet-stream", "clip_data.bin"
	}
}

// DeleteClip removes a clip and its formats
func (s *ClipService) DeleteClip(userID uint, clipID string) error {
	return database.DB.Transaction(func(tx *gorm.DB) error {
		// Verify ownership
		var clip model.Clip
		if err := tx.Where("id = ? AND user_id = ?", clipID, userID).First(&clip).Error; err != nil {
			if errors.Is(err, gorm.ErrRecordNotFound) {
				return errors.New("剪贴板不存在")
			}
			return err
		}

		// Delete formats (cascade)
		if err := tx.Where("clip_id = ?", clipID).Delete(&model.ClipFormat{}).Error; err != nil {
			return err
		}

		// Delete clip
		return tx.Delete(&clip).Error
	})
}

// SyncRequest represents the sync API request
type SyncRequest struct {
	Since     time.Time     `json:"since"`
	DeviceID  string        `json:"device_id"`
	PushClips []PushClipItem `json:"push_clips"`
}

// PushClipItem represents a clip being pushed from client
type PushClipItem struct {
	ID          string           `json:"id"`
	Description string           `json:"description"`
	CRC         int64            `json:"crc"`
	GroupID     string           `json:"group_id"`
	ShortCut    int              `json:"short_cut"`
	UpdatedAt   time.Time        `json:"updated_at"` // For LWW conflict resolution
	Formats     []PushFormatItem `json:"formats"`
}

// PushFormatItem represents a format being pushed from client
type PushFormatItem struct {
	FormatType int    `json:"format_type"`
	Data       string `json:"data"` // base64-encoded
}

// SyncResponse represents the sync API response
type SyncResponse struct {
	NewClips       []ClipDetail `json:"new_clips"`
	UpdatedCount   int          `json:"updated_count"`
	SkippedCount   int          `json:"skipped_count"` // LWW: clips skipped due to conflict
	SyncTime       string       `json:"sync_time"`
}

// Sync performs incremental sync for a user with LWW conflict resolution
func (s *ClipService) Sync(userID uint, req *SyncRequest, deviceID string) (*SyncResponse, error) {
	syncTime := time.Now()

	// Step 1: Push clips from client to server (LWW: only update if newer)
	pushedCount := 0
	skippedCount := 0
	pushedClipIDs := make([]string, 0, len(req.PushClips))
	if len(req.PushClips) > 0 {
		err := database.DB.Transaction(func(tx *gorm.DB) error {
			for _, pc := range req.PushClips {
				// Check if clip already exists (upsert by ID)
				var existing model.Clip
				err := tx.Where("id = ? AND user_id = ?", pc.ID, userID).First(&existing).Error
				if err != nil && !errors.Is(err, gorm.ErrRecordNotFound) {
					return err
				}

				if errors.Is(err, gorm.ErrRecordNotFound) {
					// New clip: always create
					// Use client-provided updated_at if available, otherwise use syncTime
					clipUpdatedAt := syncTime
					if !pc.UpdatedAt.IsZero() {
						clipUpdatedAt = pc.UpdatedAt
					}
					clip := model.Clip{
						ID:          pc.ID,
						UserID:      userID,
						DeviceID:    req.DeviceID,
						Description: pc.Description,
						CRC:         pc.CRC,
						GroupID:     pc.GroupID,
						ShortCut:    pc.ShortCut,
						CreatedAt:   clipUpdatedAt,
						UpdatedAt:   clipUpdatedAt,
						PasteCount:  0,
					}
					if err := tx.Create(&clip).Error; err != nil {
						return err
					}

					// Create formats for new clip
					for _, pf := range pc.Formats {
						data, err := base64.StdEncoding.DecodeString(pf.Data)
						if err != nil {
							return err
						}
						format := model.ClipFormat{
							ClipID:     clip.ID,
							FormatType: pf.FormatType,
							Data:       data,
							CreatedAt:  clipUpdatedAt,
						}
						if err := tx.Create(&format).Error; err != nil {
							return err
						}
					}

					pushedClipIDs = append(pushedClipIDs, clip.ID)
				} else {
					// LWW Conflict Resolution: only update if incoming is newer
					// Compare by client-provided updated_at or server-side syncTime
					incomingUpdatedAt := existing.UpdatedAt // default: use existing
					if !pc.UpdatedAt.IsZero() {
						incomingUpdatedAt = pc.UpdatedAt
					}

					if incomingUpdatedAt.After(existing.UpdatedAt) {
						// Incoming clip is newer: update
						if err := tx.Model(&existing).Updates(map[string]interface{}{
							"description": pc.Description,
							"crc":         pc.CRC,
							"group_id":    pc.GroupID,
							"short_cut":   pc.ShortCut,
							"updated_at":  incomingUpdatedAt,
						}).Error; err != nil {
							return err
						}

						// Delete existing formats and insert new ones
						if err := tx.Where("clip_id = ?", existing.ID).Delete(&model.ClipFormat{}).Error; err != nil {
							return err
						}

						for _, pf := range pc.Formats {
							data, err := base64.StdEncoding.DecodeString(pf.Data)
							if err != nil {
								return err
							}
							format := model.ClipFormat{
								ClipID:     existing.ID,
								FormatType: pf.FormatType,
								Data:       data,
								CreatedAt:  incomingUpdatedAt,
							}
							if err := tx.Create(&format).Error; err != nil {
								return err
							}
						}

						pushedClipIDs = append(pushedClipIDs, existing.ID)
					} else {
						// Existing clip is newer or same: skip (LWW)
						skippedCount++
						continue
					}
				}

				pushedCount++
			}
			return nil
		})
		if err != nil {
			return nil, err
		}
	}

	// Step 1.5: Broadcast to other devices of the same user (real-time push)
	if s.broadcaster != nil && len(pushedClipIDs) > 0 {
		for _, clipID := range pushedClipIDs {
			// Find the clip to get description
			var clip model.Clip
			if err := database.DB.Where("id = ?", clipID).First(&clip).Error; err == nil {
				s.broadcaster.BroadcastToOthers(int64(userID), nil, "clip_added", map[string]interface{}{
					"clip_id":     clipID,
					"device_id":   req.DeviceID,
					"description": clip.Description,
				})
			}
		}
	}

	// Step 2: Query clips updated since "since" from other devices
	var clips []model.Clip
	query := database.DB.Where("user_id = ? AND updated_at > ? AND device_id != ?",
		userID, req.Since, req.DeviceID).Order("updated_at DESC")

	if err := query.Find(&clips).Error; err != nil {
		return nil, err
	}

	newClips := make([]ClipDetail, 0, len(clips))
	for _, clip := range clips {
		var formats []model.ClipFormat
		if err := database.DB.Where("clip_id = ?", clip.ID).Find(&formats).Error; err != nil {
			return nil, err
		}

		formatFulls := make([]ClipFormatFull, 0, len(formats))
		for _, f := range formats {
			formatFulls = append(formatFulls, ClipFormatFull{
				FormatType: f.FormatType,
				Data:       base64.StdEncoding.EncodeToString(f.Data),
				DataSize:   len(f.Data),
			})
		}

		newClips = append(newClips, ClipDetail{
			ID:          clip.ID,
			Description: clip.Description,
			CRC:         clip.CRC,
			CreatedAt:   clip.CreatedAt.UTC().Format(time.RFC3339),
			UpdatedAt:   clip.UpdatedAt.UTC().Format(time.RFC3339),
			GroupID:     clip.GroupID,
			ShortCut:    clip.ShortCut,
			PasteCount:  clip.PasteCount,
			Formats:     formatFulls,
		})
	}

	return &SyncResponse{
		NewClips:     newClips,
		UpdatedCount: pushedCount,
		SkippedCount: skippedCount,
		SyncTime:     syncTime.UTC().Format(time.RFC3339),
	}, nil
}

// LogSyncOperation records a sync operation to the sync_logs table
func LogSyncOperation(userID uint, deviceID, action string, clipCount int, status string, errMsg string) {
	log := model.SyncLog{
		UserID:    userID,
		DeviceID:  deviceID,
		Action:    action,
		ClipCount: clipCount,
		Status:    status,
		Error:     errMsg,
		SyncedAt:  time.Now(),
	}
	database.DB.Create(&log)
}
