package service

import (
	"encoding/base64"
	"errors"
	"fmt"
	"log"
	"sort"
	"strings"
	"time"

	"ditto-cloud-server/internal/database"
	"ditto-cloud-server/internal/model"
	"ditto-cloud-server/internal/response"
	"ditto-cloud-server/internal/utils"

	"gorm.io/gorm"
)

var ErrInvalidSortBy = errors.New("无效的排序字段")

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
	ID          string           `json:"id"`
	Description string           `json:"description"`
	CRC         int64            `json:"crc"`
	CreatedAt   string           `json:"created_at"`
	UpdatedAt   string           `json:"updated_at"`
	GroupID     string           `json:"group_id"`
	GroupName   string           `json:"group_name"`
	DeviceName  string           `json:"device_name"`
	ShortCut    int              `json:"short_cut"`
	PasteCount  int              `json:"paste_count"`
	Formats     []ClipFormatMeta `json:"formats"`
}

// ClipDetail represents a full clip with format data (base64-encoded)
type ClipDetail struct {
	ID          string           `json:"id"`
	Description string           `json:"description"`
	CRC         int64            `json:"crc"`
	CreatedAt   string           `json:"created_at"`
	UpdatedAt   string           `json:"updated_at"`
	GroupID     string           `json:"group_id"`
	GroupName   string           `json:"group_name"`
	DeviceName  string           `json:"device_name"`
	ShortCut    int              `json:"short_cut"`
	PasteCount  int              `json:"paste_count"`
	Formats     []ClipFormatFull `json:"formats"`
}

// ClipFormatFull represents format with base64-encoded data
type ClipFormatFull struct {
	FormatType int    `json:"format_type"`
	Data       string `json:"data"`
	DataSize   int    `json:"data_size"`
	Encrypted  bool   `json:"encrypted"` // true if data is E2E encrypted
}

// ListClips retrieves clips for a user with pagination and optional filters
func (s *ClipService) ListClips(userID uint, page, perPage int, search, groupID, sortBy, sortOrder string) (*response.PaginatedResponse, error) {
	if page < 1 {
		page = 1
	}
	if perPage < 1 {
		perPage = 20
	}
	if perPage > 100 {
		perPage = 100
	}

	orderClause, err := buildSortClause(sortBy, sortOrder)
	if err != nil {
		return nil, err
	}

	baseQuery := database.DB.Model(&model.Clip{}).Where("user_id = ?", userID)
	if groupID != "" {
		baseQuery = baseQuery.Where("group_id = ?", groupID)
	}

	// Step 1: Determine total count
	var total int64
	baseQuery.Count(&total)

	// Step 2: Query with pagination
	isPinyin := search != "" && utils.IsPinyinQuery(search)

	var clips []model.Clip
	if search == "" {
		baseQuery.Order(orderClause).Offset((page - 1) * perPage).Limit(perPage).Find(&clips)
	} else if isPinyin {
		lowSearch := strings.ToLower(search)
		baseQuery.Where("pinyin LIKE ?", "%"+lowSearch+"%").Order(orderClause).Offset((page - 1) * perPage).Limit(perPage).Find(&clips)
	} else {
		baseQuery.Where("description LIKE ?", "%"+search+"%").Order(orderClause).Offset((page - 1) * perPage).Limit(perPage).Find(&clips)
	}

	return buildClipListResponse(clips, total, page, perPage)
}

func sortClips(clips []model.Clip, sortBy, sortOrder string) {
	desc := strings.ToUpper(sortOrder) == "DESC"
	if sortBy == "" || sortBy == "updated_at" {
		sortBy = "updated_at"
		desc = true
	}
	sort.SliceStable(clips, func(i, j int) bool {
		var less bool
		switch sortBy {
		case "created_at":
			less = clips[i].CreatedAt.Before(clips[j].CreatedAt)
		case "paste_count":
			less = clips[i].PasteCount < clips[j].PasteCount
		case "description":
			less = clips[i].Description < clips[j].Description
		default:
			less = clips[i].UpdatedAt.Before(clips[j].UpdatedAt)
		}
		if desc {
			return !less
		}
		return less
	})
}

func buildClipListResponse(clips []model.Clip, total int64, page, perPage int) (*response.PaginatedResponse, error) {
	if len(clips) == 0 {
		return &response.PaginatedResponse{
			Items:   []ClipListItem{},
			Total:   total,
			Page:    page,
			PerPage: perPage,
		}, nil
	}

	clipIDs := make([]string, len(clips))
	groupIDs := make(map[string]struct{})
	deviceIDs := make(map[string]struct{})
	for i, clip := range clips {
		clipIDs[i] = clip.ID
		if clip.GroupID != "" {
			groupIDs[clip.GroupID] = struct{}{}
		}
		if clip.DeviceID != "" {
			deviceIDs[clip.DeviceID] = struct{}{}
		}
	}
	formatsByClip, err := loadFormatsForClips(database.DB, clipIDs)
	if err != nil {
		return nil, err
	}

	groupNames := loadGroupNames(groupIDs)
	deviceNames := loadDeviceNames(deviceIDs)

	items := make([]ClipListItem, 0, len(clips))
	for _, clip := range clips {
		formats := formatsByClip[clip.ID]
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
			GroupName:   groupNames[clip.GroupID],
			DeviceName:  deviceNames[clip.DeviceID],
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
			return nil, ErrClipNotFound
		}
		return nil, err
	}

	var formats []model.ClipFormat
	if err := database.DB.Where("clip_id = ?", clipID).Find(&formats).Error; err != nil {
		return nil, err
	}

	groupName := ""
	if clip.GroupID != "" {
		var group model.Group
		if err := database.DB.Select("name").Where("id = ?", clip.GroupID).First(&group).Error; err == nil {
			groupName = group.Name
		}
	}
	deviceName := ""
	if clip.DeviceID != "" {
		var device model.Device
		if err := database.DB.Select("device_name").Where("id = ?", clip.DeviceID).First(&device).Error; err == nil {
			deviceName = device.DeviceName
		}
	}

	formatFulls := make([]ClipFormatFull, 0, len(formats))
	for _, f := range formats {
		formatFulls = append(formatFulls, ClipFormatFull{
			FormatType: f.FormatType,
			Data:       base64.StdEncoding.EncodeToString(f.Data),
			DataSize:   len(f.Data),
			Encrypted:  f.Encrypted,
		})
	}

	return &ClipDetail{
		ID:          clip.ID,
		Description: clip.Description,
		CRC:         clip.CRC,
		CreatedAt:   clip.CreatedAt.UTC().Format(time.RFC3339),
		UpdatedAt:   clip.UpdatedAt.UTC().Format(time.RFC3339),
		GroupID:     clip.GroupID,
		GroupName:   groupName,
		DeviceName:  deviceName,
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
			return nil, ErrClipNotFound
		}
		return nil, err
	}

	var format model.ClipFormat
	if err := database.DB.Where("clip_id = ? AND format_type = ?", clipID, formatType).First(&format).Error; err != nil {
		if errors.Is(err, gorm.ErrRecordNotFound) {
			return nil, ErrFormatNotFound
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
				return ErrClipNotFound
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
	Since     time.Time      `json:"since"`
	DeviceID  string         `json:"device_id"`
	PushClips []PushClipItem `json:"push_clips"`
	Limit     int            `json:"limit"` // P9 FIX: Max clips to pull (default 1000, max 5000)
}

const (
	DefaultSyncPullLimit = 1000
	MaxSyncPullLimit     = 5000
)

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
	Data       string `json:"data"`       // base64-encoded
	Encrypted  bool   `json:"encrypted"`  // true if data is E2E encrypted
}

// SyncResponse represents the sync API response
type SyncResponse struct {
	NewClips     []ClipDetail `json:"new_clips"`
	DeletedIDs   []string     `json:"deleted_ids"` // IDs of clips deleted on other devices
	UpdatedCount int          `json:"updated_count"`
	SkippedCount int          `json:"skipped_count"` // LWW: clips skipped due to conflict
	SyncTime     string       `json:"sync_time"`
	HasMore      bool         `json:"has_more"` // P9 FIX: true if more clips exist beyond this batch
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
			// MEDIUM FIX (M7): Build CRC set for existing clips to prevent cross-ID duplicates
			var existingCRCs map[string]string // crc -> clip_id
			type crcRecord struct {
				CRC int64  `gorm:"column:crc"`
				ID  string `gorm:"column:id"`
			}
			var crcRecords []crcRecord
			if err := tx.Model(&model.Clip{}).Select("id, crc").Where("user_id = ? AND is_conflict_copy = ?", userID, false).Find(&crcRecords).Error; err != nil {
				return err
			}
			existingCRCs = make(map[string]string, len(crcRecords))
			for _, r := range crcRecords {
				existingCRCs[fmt.Sprintf("%d", r.CRC)] = r.ID
			}

			for _, pc := range req.PushClips {
				// M7: CRC-based de-duplication - skip if same CRC already exists for this user
				// But skip de-dup check if CRC is 0 (meaning no CRC was provided by client)
				crcKey := fmt.Sprintf("%d", pc.CRC)
				if pc.CRC != 0 {
					if existingClipID, exists := existingCRCs[crcKey]; exists {
						// Same content already exists, skip (treat as duplicate, not conflict)
						skippedCount++
						log.Printf("[Sync] CRC duplicate, skipping clip %s (existing: %s)", pc.ID, existingClipID)
						continue
					}
				}

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
						Pinyin:      utils.ConvertToPinyin(pc.Description),
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
							Encrypted:  pf.Encrypted,
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
							"pinyin":      utils.ConvertToPinyin(pc.Description),
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
								Encrypted:  pf.Encrypted,
								CreatedAt:  incomingUpdatedAt,
							}
							if err := tx.Create(&format).Error; err != nil {
								return err
							}
						}

						pushedClipIDs = append(pushedClipIDs, existing.ID)
					} else {
						// Existing clip is newer or same: save as conflict copy for review
						skippedCount++

						// Only create conflict copy if the incoming clip has different content
						if existing.CRC != pc.CRC {
						conflictClip := model.Clip{
							ID:             fmt.Sprintf("conflict-%d-%s", time.Now().UnixNano(), pc.ID),
							UserID:         userID,
							DeviceID:       req.DeviceID,
							Description:    pc.Description,
							Pinyin:         utils.ConvertToPinyin(pc.Description),
							CRC:            pc.CRC,
							CreatedAt:      pc.UpdatedAt,
							UpdatedAt:      pc.UpdatedAt,
							GroupID:        pc.GroupID,
							ShortCut:       pc.ShortCut,
							PasteCount:     0,
							IsConflictCopy: true,
							WinClipID:      existing.ID,
						}
							if err := tx.Create(&conflictClip).Error; err != nil {
								// Log but don't fail the sync
								log.Printf("[Sync] Failed to create conflict copy for clip %s: %v", pc.ID, err)
							} else {
								// Copy formats to conflict clip
								for _, pf := range pc.Formats {
									data, err := base64.StdEncoding.DecodeString(pf.Data)
									if err != nil {
										continue
									}
									format := model.ClipFormat{
										ClipID:     conflictClip.ID,
										FormatType: pf.FormatType,
										Data:       data,
										Encrypted:  pf.Encrypted,
										CreatedAt:  pc.UpdatedAt,
									}
									tx.Create(&format)
								}
							}
						}
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
	// P9 FIX: Apply pagination limit to prevent memory exhaustion
	pullLimit := req.Limit
	if pullLimit <= 0 {
		pullLimit = DefaultSyncPullLimit
	}
	if pullLimit > MaxSyncPullLimit {
		pullLimit = MaxSyncPullLimit
	}

	var clips []model.Clip
	query := database.DB.Where("user_id = ? AND updated_at > ? AND device_id != ?",
		userID, req.Since, req.DeviceID).Order("updated_at DESC").Limit(pullLimit + 1)

	if err := query.Find(&clips).Error; err != nil {
		return nil, err
	}

	// Determine if there are more clips beyond the limit
	hasMore := len(clips) > pullLimit
	if hasMore {
		clips = clips[:pullLimit] // Trim to the requested limit
	}

	// P2 FIX: Batch-load formats instead of N+1 queries
	clipIDs := make([]string, len(clips))
	for i, clip := range clips {
		clipIDs[i] = clip.ID
	}
	formatsByClip, err := loadFormatsForClips(database.DB, clipIDs)
	if err != nil {
		return nil, err
	}

	newClips := make([]ClipDetail, 0, len(clips))
	for _, clip := range clips {
		formats := formatsByClip[clip.ID]

		formatFulls := make([]ClipFormatFull, 0, len(formats))
		for _, f := range formats {
			formatFulls = append(formatFulls, ClipFormatFull{
				FormatType: f.FormatType,
				Data:       base64.StdEncoding.EncodeToString(f.Data),
				DataSize:   len(f.Data),
				Encrypted:  f.Encrypted,
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

	// Query soft-deleted clips since 'since' timestamp (for deletion sync)
	var deletedClips []model.Clip
	if err := database.DB.Unscoped().Where("user_id = ? AND deleted_at > ? AND device_id != ?",
		userID, req.Since, req.DeviceID).Find(&deletedClips).Error; err != nil {
		log.Printf("[Sync] Error querying deleted clips: %v", err)
	}

	deletedIDs := make([]string, 0, len(deletedClips))
	for _, clip := range deletedClips {
		deletedIDs = append(deletedIDs, clip.ID)
	}

	if len(deletedIDs) > 0 {
		log.Printf("[Sync] Found %d deleted clips to sync", len(deletedIDs))
	}

	return &SyncResponse{
		NewClips:     newClips,
		DeletedIDs:   deletedIDs,
		UpdatedCount: pushedCount,
		SkippedCount: skippedCount,
		SyncTime:     syncTime.UTC().Format(time.RFC3339),
		HasMore:      hasMore,
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

// ListConflictClips returns paginated conflict clips for a user
func (s *ClipService) ListConflictClips(userID uint, page, perPage int) (*response.PaginatedResponse, error) {
	if page < 1 {
		page = 1
	}
	if perPage < 1 {
		perPage = 20
	}
	if perPage > 100 {
		perPage = 100
	}

	query := database.DB.Model(&model.Clip{}).Where("user_id = ? AND is_conflict_copy = ?", userID, true)

	var total int64
	if err := query.Count(&total).Error; err != nil {
		return nil, err
	}

	var clips []model.Clip
	if err := query.Order("updated_at DESC").Offset((page - 1) * perPage).Limit(perPage).Find(&clips).Error; err != nil {
		return nil, err
	}

	clipIDs := make([]string, len(clips))
	groupIDs := make(map[string]struct{})
	deviceIDs := make(map[string]struct{})
	for i, clip := range clips {
		clipIDs[i] = clip.ID
		if clip.GroupID != "" {
			groupIDs[clip.GroupID] = struct{}{}
		}
		if clip.DeviceID != "" {
			deviceIDs[clip.DeviceID] = struct{}{}
		}
	}
	formatsByClip, err := loadFormatsForClips(database.DB, clipIDs)
	if err != nil {
		return nil, err
	}

	groupNames := loadGroupNames(groupIDs)
	deviceNames := loadDeviceNames(deviceIDs)

	items := make([]ClipListItem, 0, len(clips))
	for _, clip := range clips {
		formats := formatsByClip[clip.ID]

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
			GroupName:   groupNames[clip.GroupID],
			DeviceName:  deviceNames[clip.DeviceID],
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

// ResolveConflictClip resolves a conflict clip by either accepting it (replacing the winning clip) or discarding it
func (s *ClipService) ResolveConflictClip(userID uint, conflictClipID string, action string) error {
	var conflictClip model.Clip
	if err := database.DB.Where("id = ? AND user_id = ? AND is_conflict_copy = ?", conflictClipID, userID, true).
		First(&conflictClip).Error; err != nil {
		if errors.Is(err, gorm.ErrRecordNotFound) {
			return ErrConflictClipNotFound
		}
		return err
	}

	if action == "accept" {
		// Prefer WinClipID if set (P0 fix), fall back to CRC match for legacy data
		var winningClip model.Clip
		var winErr error
		if conflictClip.WinClipID != "" {
			winErr = database.DB.Where("id = ? AND user_id = ?", conflictClip.WinClipID, userID).
				First(&winningClip).Error
		}
		if winErr != nil || conflictClip.WinClipID == "" {
			winErr = database.DB.Where("user_id = ? AND crc = ? AND is_conflict_copy = ? AND id != ?",
				userID, conflictClip.CRC, false, conflictClipID).
				Order("updated_at DESC").First(&winningClip).Error
		}
		if winErr == nil {
			// Replace winning clip's content with conflict clip's content
			if err := database.DB.Model(&winningClip).Updates(map[string]interface{}{
				"description": conflictClip.Description,
				"pinyin":      utils.ConvertToPinyin(conflictClip.Description),
				"updated_at":  conflictClip.UpdatedAt,
			}).Error; err != nil {
				return err
			}
		}
	}

	// Delete the conflict clip and its formats
	if err := database.DB.Transaction(func(tx *gorm.DB) error {
		if err := tx.Where("clip_id = ?", conflictClipID).Delete(&model.ClipFormat{}).Error; err != nil {
			return err
		}
		return tx.Delete(&conflictClip).Error
	}); err != nil {
		return err
	}

	return nil
}

// loadFormatsForClips batch-loads all formats for the given clip IDs.
// P1+P2 FIX: Replaces N+1 query pattern (one query per clip) with a single IN query.
// Returns a map from clip ID to its formats.
func loadFormatsForClips(db *gorm.DB, clipIDs []string) (map[string][]model.ClipFormat, error) {
	if len(clipIDs) == 0 {
		return make(map[string][]model.ClipFormat), nil
	}

	var allFormats []model.ClipFormat
	if err := db.Where("clip_id IN ?", clipIDs).Order("clip_id, format_type").Find(&allFormats).Error; err != nil {
		return nil, err
	}

	// Group by clip_id
	formatsByClip := make(map[string][]model.ClipFormat, len(clipIDs))
	for _, f := range allFormats {
		formatsByClip[f.ClipID] = append(formatsByClip[f.ClipID], f)
	}

	return formatsByClip, nil
}

func loadGroupNames(groupIDs map[string]struct{}) map[string]string {
	result := map[string]string{}
	if len(groupIDs) == 0 {
		return result
	}
	ids := make([]string, 0, len(groupIDs))
	for id := range groupIDs {
		ids = append(ids, id)
	}
	var groups []model.Group
	if err := database.DB.Select("id, name").Where("id IN ?", ids).Find(&groups).Error; err != nil {
		return result
	}
	for _, g := range groups {
		result[g.ID] = g.Name
	}
	return result
}

func loadDeviceNames(deviceIDs map[string]struct{}) map[string]string {
	result := map[string]string{}
	if len(deviceIDs) == 0 {
		return result
	}
	ids := make([]string, 0, len(deviceIDs))
	for id := range deviceIDs {
		ids = append(ids, id)
	}
	var devices []model.Device
	if err := database.DB.Select("id, device_name").Where("id IN ?", ids).Find(&devices).Error; err != nil {
		return result
	}
	for _, d := range devices {
		result[d.ID] = d.DeviceName
	}
	return result
}

var allowedSortBy = map[string]bool{
	"created_at":  true,
	"updated_at":  true,
	"paste_count": true,
	"description": true,
}

func buildSortClause(sortBy, sortOrder string) (string, error) {
	if sortBy == "" {
		return "updated_at DESC", nil
	}
	if !allowedSortBy[sortBy] {
		return "", ErrInvalidSortBy
	}
	order := "ASC"
	if strings.ToUpper(sortOrder) == "DESC" {
		order = "DESC"
	}
	return sortBy + " " + order, nil
}
