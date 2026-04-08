package handler

import (
	"net/http"

	"ditto-cloud-server/internal/middleware"
	"ditto-cloud-server/internal/response"
	"ditto-cloud-server/internal/service"

	"github.com/gin-gonic/gin"
)

type GroupHandler struct {
	service *service.GroupService
}

func NewGroupHandler(svc *service.GroupService) *GroupHandler {
	return &GroupHandler{service: svc}
}

// ListGroups handles GET /api/v1/groups
func (h *GroupHandler) ListGroups(c *gin.Context) {
	userID := middleware.GetUserID(c)

	groups, err := h.service.ListGroups(userID)
	if err != nil {
		response.Error(c, http.StatusInternalServerError, 50000, "获取分组列表失败: "+err.Error())
		return
	}

	response.Success(c, groups)
}

// GetGroup handles GET /api/v1/groups/:id
func (h *GroupHandler) GetGroup(c *gin.Context) {
	userID := middleware.GetUserID(c)
	groupID := c.Param("id")

	group, err := h.service.GetGroup(userID, groupID)
	if err != nil {
		if err.Error() == "分组不存在" {
			response.Error(c, http.StatusNotFound, 40400, err.Error())
			return
		}
		response.Error(c, http.StatusInternalServerError, 50000, "获取分组失败: "+err.Error())
		return
	}

	response.Success(c, group)
}

// CreateGroup handles POST /api/v1/groups
func (h *GroupHandler) CreateGroup(c *gin.Context) {
	userID := middleware.GetUserID(c)

	var req service.CreateGroupRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		response.Error(c, http.StatusBadRequest, 40000, "请求参数错误: "+err.Error())
		return
	}

	group, err := h.service.CreateGroup(userID, &req)
	if err != nil {
		response.Error(c, http.StatusInternalServerError, 50000, "创建分组失败: "+err.Error())
		return
	}

	response.Success(c, group)
}

// UpdateGroup handles PUT /api/v1/groups/:id
func (h *GroupHandler) UpdateGroup(c *gin.Context) {
	userID := middleware.GetUserID(c)
	groupID := c.Param("id")

	var req service.UpdateGroupRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		response.Error(c, http.StatusBadRequest, 40000, "请求参数错误: "+err.Error())
		return
	}

	group, err := h.service.UpdateGroup(userID, groupID, &req)
	if err != nil {
		if err.Error() == "分组不存在" {
			response.Error(c, http.StatusNotFound, 40400, err.Error())
			return
		}
		response.Error(c, http.StatusInternalServerError, 50000, "更新分组失败: "+err.Error())
		return
	}

	response.Success(c, group)
}

// DeleteGroup handles DELETE /api/v1/groups/:id
func (h *GroupHandler) DeleteGroup(c *gin.Context) {
	userID := middleware.GetUserID(c)
	groupID := c.Param("id")

	if err := h.service.DeleteGroup(userID, groupID); err != nil {
		if err.Error() == "分组不存在" {
			response.Error(c, http.StatusNotFound, 40400, err.Error())
			return
		}
		response.Error(c, http.StatusInternalServerError, 50000, "删除分组失败: "+err.Error())
		return
	}

	response.SuccessWithMessage(c, "分组已删除", nil)
}

// MoveClipsToGroup handles POST /api/v1/groups/:id/move-clips
func (h *GroupHandler) MoveClipsToGroup(c *gin.Context) {
	userID := middleware.GetUserID(c)
	groupID := c.Param("id")

	var req struct {
		ClipIDs []string `json:"clip_ids" binding:"required"`
	}
	if err := c.ShouldBindJSON(&req); err != nil {
		response.Error(c, http.StatusBadRequest, 40000, "请求参数错误: "+err.Error())
		return
	}

	if err := h.service.MoveClipsToGroup(userID, groupID, req.ClipIDs); err != nil {
		if err.Error() == "分组不存在" {
			response.Error(c, http.StatusNotFound, 40400, err.Error())
			return
		}
		response.Error(c, http.StatusInternalServerError, 50000, "移动剪贴板失败: "+err.Error())
		return
	}

	response.SuccessWithMessage(c, "剪贴板已移动", nil)
}

// RemoveClipsFromGroup handles POST /api/v1/clips/remove-from-group
func (h *GroupHandler) RemoveClipsFromGroup(c *gin.Context) {
	userID := middleware.GetUserID(c)

	var req struct {
		ClipIDs []string `json:"clip_ids" binding:"required"`
	}
	if err := c.ShouldBindJSON(&req); err != nil {
		response.Error(c, http.StatusBadRequest, 40000, "请求参数错误: "+err.Error())
		return
	}

	if err := h.service.RemoveClipsFromGroup(userID, req.ClipIDs); err != nil {
		response.Error(c, http.StatusInternalServerError, 50000, "移除剪贴板失败: "+err.Error())
		return
	}

	response.SuccessWithMessage(c, "剪贴板已从分组移除", nil)
}
