package handler

import (
	"log"
	"net/http"
	"strconv"

	"ditto-cloud-server/internal/database"
	"ditto-cloud-server/internal/model"
	"ditto-cloud-server/internal/response"
	"ditto-cloud-server/pkg/crypto"

	"github.com/gin-gonic/gin"
)

type AdminHandler struct{}

func NewAdminHandler() *AdminHandler {
	return &AdminHandler{}
}

type CreateUserRequest struct {
	Username string `json:"username" binding:"required,min=3,max=32"`
	Email    string `json:"email" binding:"required,email"`
	Password string `json:"password" binding:"required,min=6,max=64"`
}

type UpdateUserRequest struct {
	Email    *string `json:"email,omitempty"`
	Password *string `json:"password,omitempty"`
	IsActive *bool   `json:"is_active,omitempty"`
	Role     *string `json:"role,omitempty"`
}

type ResetPasswordRequest struct {
	Password string `json:"password" binding:"required,min=6,max=64"`
}

func (h *AdminHandler) CreateUser(c *gin.Context) {
	var req CreateUserRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		response.Error(c, http.StatusBadRequest, 40000, "请求参数错误: "+err.Error())
		return
	}

	// Check if username exists
	var existing model.User
	if err := database.DB.Where("username = ?", req.Username).First(&existing).Error; err == nil {
		response.Error(c, http.StatusBadRequest, 40001, "用户名已存在")
		return
	}

	// Check if email exists
	if err := database.DB.Where("email = ?", req.Email).First(&existing).Error; err == nil {
		response.Error(c, http.StatusBadRequest, 40002, "邮箱已被注册")
		return
	}

	hashedPassword, err := crypto.HashPassword(req.Password)
	if err != nil {
		response.Error(c, http.StatusInternalServerError, 50000, "密码加密失败")
		return
	}

	user := model.User{
		Username:     req.Username,
		Email:        req.Email,
		PasswordHash: hashedPassword,
		Role:         "user",
		IsActive:     true,
	}

	if err := database.DB.Create(&user).Error; err != nil {
		log.Printf("[CreateUser] error: %v", err)
		response.Error(c, http.StatusInternalServerError, 50000, "创建用户失败")
		return
	}

	response.SuccessWithMessage(c, "用户创建成功", gin.H{
		"user_id": user.ID,
	})
}

func (h *AdminHandler) ListUsers(c *gin.Context) {
	page, _ := strconv.Atoi(c.DefaultQuery("page", "1"))
	perPage, _ := strconv.Atoi(c.DefaultQuery("per_page", "20"))
	if page < 1 {
		page = 1
	}
	if perPage < 1 || perPage > 100 {
		perPage = 20
	}

	search := c.Query("search")

	var total int64
	query := database.DB.Model(&model.User{})
	if search != "" {
		like := "%" + search + "%"
		query = query.Where("username LIKE ? OR email LIKE ?", like, like)
	}
	query.Count(&total)

	var users []struct {
		model.User
		DeviceCount int64 `json:"device_count"`
	}

	offset := (page - 1) * perPage
	rows := database.DB.Model(&model.User{}).
		Select("users.*, COALESCE(dc.device_count, 0) as device_count").
		Joins("LEFT JOIN (SELECT user_id, COUNT(*) as device_count FROM devices GROUP BY user_id) dc ON dc.user_id = users.id").
		Order("users.id DESC").
		Limit(perPage).
		Offset(offset)

	if search != "" {
		like := "%" + search + "%"
		rows = rows.Where("users.username LIKE ? OR users.email LIKE ?", like, like)
	}

	if err := rows.Find(&users).Error; err != nil {
		log.Printf("[ListUsers] error: %v", err)
		response.Error(c, http.StatusInternalServerError, 50000, "查询用户列表失败")
		return
	}

	response.Success(c, response.PaginatedResponse{
		Items:   users,
		Total:   total,
		Page:    page,
		PerPage: perPage,
	})
}

func (h *AdminHandler) GetUser(c *gin.Context) {
	id, err := strconv.ParseUint(c.Param("id"), 10, 64)
	if err != nil {
		response.Error(c, http.StatusBadRequest, 40000, "无效的用户ID")
		return
	}

	var user model.User
	if err := database.DB.First(&user, id).Error; err != nil {
		response.Error(c, http.StatusNotFound, 40400, "用户不存在")
		return
	}

	var deviceCount int64
	database.DB.Model(&model.Device{}).Where("user_id = ?", user.ID).Count(&deviceCount)

	response.Success(c, gin.H{
		"id":           user.ID,
		"username":     user.Username,
		"email":        user.Email,
		"role":         user.Role,
		"is_active":    user.IsActive,
		"device_count": deviceCount,
		"created_at":   user.CreatedAt,
		"updated_at":   user.UpdatedAt,
	})
}

func (h *AdminHandler) UpdateUser(c *gin.Context) {
	id, err := strconv.ParseUint(c.Param("id"), 10, 64)
	if err != nil {
		response.Error(c, http.StatusBadRequest, 40000, "无效的用户ID")
		return
	}

	var req UpdateUserRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		response.Error(c, http.StatusBadRequest, 40000, "请求参数错误: "+err.Error())
		return
	}

	var user model.User
	if err := database.DB.First(&user, id).Error; err != nil {
		response.Error(c, http.StatusNotFound, 40400, "用户不存在")
		return
	}

	updates := map[string]interface{}{}

	if req.Email != nil {
		// Check email uniqueness
		var existing model.User
		if err := database.DB.Where("email = ? AND id != ?", *req.Email, id).First(&existing).Error; err == nil {
			response.Error(c, http.StatusBadRequest, 40002, "邮箱已被其他用户使用")
			return
		}
		updates["email"] = *req.Email
	}

	if req.Password != nil {
		hashedPassword, err := crypto.HashPassword(*req.Password)
		if err != nil {
			response.Error(c, http.StatusInternalServerError, 50000, "密码加密失败")
			return
		}
		updates["password_hash"] = hashedPassword
	}

	if req.IsActive != nil {
		updates["is_active"] = *req.IsActive
	}

	if req.Role != nil {
		role := *req.Role
		if role != "admin" && role != "user" {
			response.Error(c, http.StatusBadRequest, 40000, "角色无效，必须是 admin 或 user")
			return
		}
		updates["role"] = role
	}

	if len(updates) > 0 {
		if err := database.DB.Model(&user).Updates(updates).Error; err != nil {
			log.Printf("[UpdateUser] error: %v", err)
			response.Error(c, http.StatusInternalServerError, 50000, "更新用户失败")
			return
		}
	}

	response.SuccessWithMessage(c, "用户更新成功", nil)
}

func (h *AdminHandler) DeleteUser(c *gin.Context) {
	id, err := strconv.ParseUint(c.Param("id"), 10, 64)
	if err != nil {
		response.Error(c, http.StatusBadRequest, 40000, "无效的用户ID")
		return
	}

	var user model.User
	if err := database.DB.First(&user, id).Error; err != nil {
		response.Error(c, http.StatusNotFound, 40400, "用户不存在")
		return
	}

	// Prevent deleting the last admin
	var adminCount int64
	database.DB.Model(&model.User{}).Where("role = ?", "admin").Count(&adminCount)
	if user.Role == "admin" && adminCount <= 1 {
		response.Error(c, http.StatusBadRequest, 40003, "无法删除最后一个管理员账号")
		return
	}

	// Hard delete: delete all related data first, then user
	// Delete clip formats (via clips), clips, groups, devices, sync logs, encryption settings
	tx := database.DB.Begin()

	// Find all clips belonging to this user
	var clipIDs []uint
	tx.Model(&model.Clip{}).Where("user_id = ?", user.ID).Pluck("id", &clipIDs)

	if len(clipIDs) > 0 {
		tx.Where("clip_id IN ?", clipIDs).Delete(&model.ClipFormat{})
		tx.Delete(&model.Clip{}, clipIDs)
	}

	tx.Where("user_id = ?", user.ID).Delete(&model.Device{})
	tx.Where("user_id = ?", user.ID).Delete(&model.Group{})
	tx.Where("user_id = ?", user.ID).Delete(&model.SyncLog{})
	tx.Where("user_id = ?", user.ID).Delete(&model.EncryptionSettings{})
	tx.Delete(&user)

	if err := tx.Commit().Error; err != nil {
		log.Printf("[DeleteUser] error: %v", err)
		response.Error(c, http.StatusInternalServerError, 50000, "删除用户失败")
		return
	}

	response.SuccessWithMessage(c, "用户已删除", nil)
}

func (h *AdminHandler) ResetPassword(c *gin.Context) {
	id, err := strconv.ParseUint(c.Param("id"), 10, 64)
	if err != nil {
		response.Error(c, http.StatusBadRequest, 40000, "无效的用户ID")
		return
	}

	var req ResetPasswordRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		response.Error(c, http.StatusBadRequest, 40000, "请求参数错误: "+err.Error())
		return
	}

	var user model.User
	if err := database.DB.First(&user, id).Error; err != nil {
		response.Error(c, http.StatusNotFound, 40400, "用户不存在")
		return
	}

	hashedPassword, err := crypto.HashPassword(req.Password)
	if err != nil {
		response.Error(c, http.StatusInternalServerError, 50000, "密码加密失败")
		return
	}

	if err := database.DB.Model(&user).Update("password_hash", hashedPassword).Error; err != nil {
		log.Printf("[ResetPassword] error: %v", err)
		response.Error(c, http.StatusInternalServerError, 50000, "重置密码失败")
		return
	}

	response.SuccessWithMessage(c, "密码已重置", nil)
}