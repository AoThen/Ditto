package middleware

import (
	"net/http"

	"ditto-cloud-server/internal/database"
	"ditto-cloud-server/internal/model"
	"ditto-cloud-server/internal/response"

	"github.com/gin-gonic/gin"
)

func AdminAuth() gin.HandlerFunc {
	return func(c *gin.Context) {
		userID := GetUserID(c)
		if userID == 0 {
			response.Error(c, http.StatusUnauthorized, 40100, "未提供认证令牌")
			c.Abort()
			return
		}

		var user model.User
		if err := database.DB.First(&user, userID).Error; err != nil {
			response.Error(c, http.StatusForbidden, 40300, "用户不存在")
			c.Abort()
			return
		}

		if user.Role != "admin" {
			response.Error(c, http.StatusForbidden, 40301, "需要管理员权限")
			c.Abort()
			return
		}

		c.Set("user_role", user.Role)
		c.Next()
	}
}