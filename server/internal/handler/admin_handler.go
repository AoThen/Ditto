package handler

import (
	"net/http"
	"strconv"

	"ditto-cloud-server/internal/response"
	"ditto-cloud-server/internal/service"

	"github.com/gin-gonic/gin"
)

type AdminHandler struct {
	userService *service.UserService
}

func NewAdminHandler(userService *service.UserService) *AdminHandler {
	return &AdminHandler{userService: userService}
}

type CreateUserRequest struct {
	Username string `json:"username" binding:"required,min=3,max=32"`
	Email    string `json:"email" binding:"required,email"`
	Password string `json:"password" binding:"required,min=6,max=64"`
}

type UpdateUserRequest struct {
	Email *string `json:"email,omitempty"`
	// min/max without omitempty on purpose: a nil pointer is skipped, but an
	// explicit empty string must be rejected instead of silently accepted.
	Password *string `json:"password,omitempty" binding:"min=6,max=64"`
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

	user, err := h.userService.CreateUser(req.Username, req.Email, req.Password)
	if err != nil {
		if err.Error() == "用户名已存在" {
			response.Error(c, http.StatusBadRequest, 40001, err.Error())
			return
		}
		if err.Error() == "邮箱已被注册" {
			response.Error(c, http.StatusBadRequest, 40002, err.Error())
			return
		}
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

	users, total, err := h.userService.ListUsers(search, page, perPage)
	if err != nil {
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

	user, err := h.userService.GetUser(uint(id))
	if err != nil {
		response.Error(c, http.StatusNotFound, 40400, "用户不存在")
		return
	}

	deviceCount, _ := h.userService.GetDeviceCount(user.ID)

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

	if err := h.userService.CheckUserExists(uint(id)); err != nil {
		response.Error(c, http.StatusNotFound, 40400, "用户不存在")
		return
	}

	updates := map[string]interface{}{}

	if req.Email != nil {
		if err := h.userService.CheckEmailTakenByOther(*req.Email, uint(id)); err != nil {
			response.Error(c, http.StatusBadRequest, 40002, err.Error())
			return
		}
		updates["email"] = *req.Email
	}

	if req.Password != nil {
		if err := h.userService.ResetPassword(uint(id), *req.Password); err != nil {
			response.Error(c, http.StatusInternalServerError, 50000, "密码加密失败")
			return
		}
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
		if err := h.userService.UpdateUser(uint(id), updates); err != nil {
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

	if err := h.userService.DeleteUser(uint(id)); err != nil {
		if err.Error() == "无法删除最后一个管理员账号" {
			response.Error(c, http.StatusBadRequest, 40003, err.Error())
			return
		}
		response.Error(c, http.StatusNotFound, 40400, err.Error())
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

	if err := h.userService.CheckUserExists(uint(id)); err != nil {
		response.Error(c, http.StatusNotFound, 40400, "用户不存在")
		return
	}

	if err := h.userService.ResetPassword(uint(id), req.Password); err != nil {
		response.Error(c, http.StatusInternalServerError, 50000, "重置密码失败")
		return
	}

	response.SuccessWithMessage(c, "密码已重置", nil)
}