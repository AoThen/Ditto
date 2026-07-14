package service

import (
	"errors"
	"fmt"
	"time"

	"ditto-cloud-server/internal/database"
	"ditto-cloud-server/internal/model"
	"ditto-cloud-server/internal/response"

	"github.com/google/uuid"
	"gorm.io/gorm"
)

type GroupService struct{}

func NewGroupService() *GroupService {
	return &GroupService{}
}

// GroupListItem represents a group for list responses
type GroupListItem struct {
	ID          string  `json:"id"`
	Name        string  `json:"name"`
	Description string  `json:"description"`
	ParentID    string  `json:"parent_id"`
	ClipOrder   float64 `json:"clip_order"`
	CreatedAt   string  `json:"created_at"`
	UpdatedAt   string  `json:"updated_at"`
	ClipCount   int64   `json:"clip_count"`
}

// GroupDetail represents a group with child groups
type GroupDetail struct {
	ID          string          `json:"id"`
	Name        string          `json:"name"`
	Description string          `json:"description"`
	ParentID    string          `json:"parent_id"`
	ClipOrder   float64         `json:"clip_order"`
	CreatedAt   string          `json:"created_at"`
	UpdatedAt   string          `json:"updated_at"`
	ClipCount   int64           `json:"clip_count"`
	Children    []GroupListItem `json:"children"`
}

// ListGroups retrieves all groups for a user with clip counts
func (s *GroupService) ListGroups(userID uint, page, perPage int) (*response.PaginatedResponse, error) {
	if page < 1 {
		page = 1
	}
	if perPage < 1 {
		perPage = 20
	}
	if perPage > 100 {
		perPage = 100
	}

	var total int64
	if err := database.DB.Model(&model.Group{}).Where("user_id = ?", userID).Count(&total).Error; err != nil {
		return nil, err
	}

	var groups []model.Group
	if err := database.DB.Where("user_id = ?", userID).Order("clip_order ASC, created_at ASC").Offset((page - 1) * perPage).Limit(perPage).Find(&groups).Error; err != nil {
		return nil, err
	}

	groupIDs := make([]string, len(groups))
	for i, g := range groups {
		groupIDs[i] = g.ID
	}

	type GroupCount struct {
		GroupID string
		Count   int64
	}
	var counts []GroupCount
	if len(groupIDs) > 0 {
		database.DB.Model(&model.Clip{}).
			Select("group_id, COUNT(*) as count").
			Where("group_id IN ?", groupIDs).
			Group("group_id").
			Scan(&counts)
	}
	countMap := map[string]int64{}
	for _, c := range counts {
		countMap[c.GroupID] = c.Count
	}

	items := make([]GroupListItem, 0, len(groups))
	for _, g := range groups {
		parentID := ""
		if g.ParentID != nil {
			parentID = *g.ParentID
		}

		items = append(items, GroupListItem{
			ID:          g.ID,
			Name:        g.Name,
			Description: g.Description,
			ParentID:    parentID,
			ClipOrder:   g.ClipOrder,
			CreatedAt:   g.CreatedAt.UTC().Format(time.RFC3339),
			UpdatedAt:   g.UpdatedAt.UTC().Format(time.RFC3339),
			ClipCount:   countMap[g.ID],
		})
	}

	return &response.PaginatedResponse{
		Items:   items,
		Total:   total,
		Page:    page,
		PerPage: perPage,
	}, nil
}

// GetGroup retrieves a group with children
func (s *GroupService) GetGroup(userID uint, groupID string) (*GroupDetail, error) {
	var group model.Group
	if err := database.DB.Where("id = ? AND user_id = ?", groupID, userID).First(&group).Error; err != nil {
		if errors.Is(err, gorm.ErrRecordNotFound) {
			return nil, ErrGroupNotFound
		}
		return nil, err
	}

	var clipCount int64
	database.DB.Model(&model.Clip{}).Where("user_id = ? AND group_id = ?", userID, group.ID).Count(&clipCount)

	var children []model.Group
	if err := database.DB.Where("user_id = ? AND parent_id = ?", userID, group.ID).Order("clip_order ASC").Find(&children).Error; err != nil {
		return nil, err
	}

	childIDs := make([]string, len(children))
	for i, c := range children {
		childIDs[i] = c.ID
	}
	type GroupCount struct {
		GroupID string
		Count   int64
	}
	var childCounts []GroupCount
	if len(childIDs) > 0 {
		database.DB.Model(&model.Clip{}).
			Select("group_id, COUNT(*) as count").
			Where("group_id IN ?", childIDs).
			Group("group_id").
			Scan(&childCounts)
	}
	childCountMap := map[string]int64{}
	for _, cc := range childCounts {
		childCountMap[cc.GroupID] = cc.Count
	}

	childItems := make([]GroupListItem, 0, len(children))
	for _, c := range children {
		childParentID := ""
		if c.ParentID != nil {
			childParentID = *c.ParentID
		}

		childItems = append(childItems, GroupListItem{
			ID:          c.ID,
			Name:        c.Name,
			Description: c.Description,
			ParentID:    childParentID,
			ClipOrder:   c.ClipOrder,
			CreatedAt:   c.CreatedAt.UTC().Format(time.RFC3339),
			UpdatedAt:   c.UpdatedAt.UTC().Format(time.RFC3339),
			ClipCount:   childCountMap[c.ID],
		})
	}

	parentID := ""
	if group.ParentID != nil {
		parentID = *group.ParentID
	}

	return &GroupDetail{
		ID:          group.ID,
		Name:        group.Name,
		Description: group.Description,
		ParentID:    parentID,
		ClipOrder:   group.ClipOrder,
		CreatedAt:   group.CreatedAt.UTC().Format(time.RFC3339),
		UpdatedAt:   group.UpdatedAt.UTC().Format(time.RFC3339),
		ClipCount:   clipCount,
		Children:    childItems,
	}, nil
}

// CreateGroupRequest represents the create group request
type CreateGroupRequest struct {
	Name        string  `json:"name" binding:"required"`
	Description string  `json:"description"`
	ParentID    *string `json:"parent_id"`
	ClipOrder   float64 `json:"clip_order"`
}

// CreateGroup creates a new group
func (s *GroupService) CreateGroup(userID uint, req *CreateGroupRequest) (*GroupListItem, error) {
	// Validate parent exists if provided
	if req.ParentID != nil && *req.ParentID != "" {
		var parent model.Group
		if err := database.DB.Where("id = ? AND user_id = ?", *req.ParentID, userID).First(&parent).Error; err != nil {
			if errors.Is(err, gorm.ErrRecordNotFound) {
				return nil, errors.New("父分组不存在")
			}
			return nil, err
		}
	}

	id := fmt.Sprintf("grp-%s", uuid.New().String()[:8])

	group := model.Group{
		ID:          id,
		UserID:      userID,
		Name:        req.Name,
		Description: req.Description,
		ParentID:    req.ParentID,
		ClipOrder:   req.ClipOrder,
	}

	if err := database.DB.Create(&group).Error; err != nil {
		return nil, err
	}

	parentID := ""
	if group.ParentID != nil {
		parentID = *group.ParentID
	}

	return &GroupListItem{
		ID:          group.ID,
		Name:        group.Name,
		Description: group.Description,
		ParentID:    parentID,
		ClipOrder:   group.ClipOrder,
		CreatedAt:   group.CreatedAt.UTC().Format(time.RFC3339),
		UpdatedAt:   group.UpdatedAt.UTC().Format(time.RFC3339),
		ClipCount:   0,
	}, nil
}

// UpdateGroupRequest represents the update group request
type UpdateGroupRequest struct {
	Name        string  `json:"name"`
	Description string  `json:"description"`
	ParentID    *string `json:"parent_id"`
	ClipOrder   float64 `json:"clip_order"`
}

// UpdateGroup updates a group and returns the updated group
func (s *GroupService) UpdateGroup(userID uint, groupID string, req *UpdateGroupRequest) (*GroupListItem, error) {
	var group model.Group
	if err := database.DB.Where("id = ? AND user_id = ?", groupID, userID).First(&group).Error; err != nil {
		if errors.Is(err, gorm.ErrRecordNotFound) {
				return nil, ErrGroupNotFound
			}
			return nil, err
		}

		// Validate parent exists and isn't self
	if req.ParentID != nil && *req.ParentID != "" {
		if *req.ParentID == groupID {
			return nil, errors.New("不能将分组设为自身")
		}
		var parent model.Group
		if err := database.DB.Where("id = ? AND user_id = ?", *req.ParentID, userID).First(&parent).Error; err != nil {
			if errors.Is(err, gorm.ErrRecordNotFound) {
				return nil, errors.New("父分组不存在")
			}
			return nil, err
		}
	}

	updates := map[string]interface{}{}
	if req.Name != "" {
		updates["name"] = req.Name
	}
	// M9 FIX: Only update description if explicitly provided (consistent with name behavior)
	if req.Description != "" {
		updates["description"] = req.Description
	}
	if req.ParentID != nil {
		updates["parent_id"] = req.ParentID
	}
	updates["clip_order"] = req.ClipOrder
	updates["updated_at"] = time.Now()

	if err := database.DB.Model(&group).Updates(updates).Error; err != nil {
		return nil, err
	}

	// Reload
	if err := database.DB.Where("id = ? AND user_id = ?", groupID, userID).First(&group).Error; err != nil {
		return nil, err
	}

	var clipCount int64
	database.DB.Model(&model.Clip{}).Where("user_id = ? AND group_id = ?", userID, group.ID).Count(&clipCount)

	parentID := ""
	if group.ParentID != nil {
		parentID = *group.ParentID
	}

	return &GroupListItem{
		ID:          group.ID,
		Name:        group.Name,
		Description: group.Description,
		ParentID:    parentID,
		ClipOrder:   group.ClipOrder,
		CreatedAt:   group.CreatedAt.UTC().Format(time.RFC3339),
		UpdatedAt:   group.UpdatedAt.UTC().Format(time.RFC3339),
		ClipCount:   clipCount,
	}, nil
}

// DeleteGroup deletes a group (does NOT delete clips in it)
func (s *GroupService) DeleteGroup(userID uint, groupID string) error {
	return database.DB.Transaction(func(tx *gorm.DB) error {
		var group model.Group
		if err := tx.Where("id = ? AND user_id = ?", groupID, userID).First(&group).Error; err != nil {
			if errors.Is(err, gorm.ErrRecordNotFound) {
				return ErrGroupNotFound
			}
			return err
		}

		// M8 FIX: Check error when unsetting group_id from clips
		if err := tx.Model(&model.Clip{}).Where("user_id = ? AND group_id = ?", userID, groupID).Update("group_id", "").Error; err != nil {
			return err
		}

		// Delete child groups recursively
		var children []model.Group
		if err := tx.Where("user_id = ? AND parent_id = ?", userID, group.ID).Find(&children).Error; err != nil {
			return err
		}
		for _, child := range children {
			if err := s.deleteGroupRecursive(tx, userID, child.ID); err != nil {
				return err
			}
		}

		// Delete the group itself
		return tx.Delete(&group).Error
	})
}

func (s *GroupService) deleteGroupRecursive(tx *gorm.DB, userID uint, groupID string) error {
	// M8 FIX: Check error when unsetting group_id from clips
	if err := tx.Model(&model.Clip{}).Where("user_id = ? AND group_id = ?", userID, groupID).Update("group_id", "").Error; err != nil {
		return err
	}

	// Delete children
	var children []model.Group
	if err := tx.Where("user_id = ? AND parent_id = ?", userID, groupID).Find(&children).Error; err != nil {
		return err
	}
	for _, child := range children {
		if err := s.deleteGroupRecursive(tx, userID, child.ID); err != nil {
			return err
		}
	}

	return tx.Where("id = ? AND user_id = ?", groupID, userID).Delete(&model.Group{}).Error
}

// MoveClipsToGroup moves clips to a group
func (s *GroupService) MoveClipsToGroup(userID uint, groupID string, clipIDs []string) error {
	// Verify group exists
	var group model.Group
	if err := database.DB.Where("id = ? AND user_id = ?", groupID, userID).First(&group).Error; err != nil {
		if errors.Is(err, gorm.ErrRecordNotFound) {
			return ErrGroupNotFound
		}
		return err
	}

	return database.DB.Model(&model.Clip{}).
		Where("user_id = ? AND id IN ?", userID, clipIDs).
		Update("group_id", groupID).Error
}

// RemoveClipsFromGroup removes clips from a group (sets group_id to empty)
func (s *GroupService) RemoveClipsFromGroup(userID uint, clipIDs []string) error {
	return database.DB.Model(&model.Clip{}).
		Where("user_id = ? AND id IN ?", userID, clipIDs).
		Update("group_id", "").Error
}
