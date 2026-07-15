package handler

import (
	"log"
	"net/http"
	"strconv"

	"ditto-cloud-server/internal/middleware"
	"ditto-cloud-server/internal/response"
	"ditto-cloud-server/internal/service"

	"github.com/gin-gonic/gin"
)

type DeviceHandler struct {
	service *service.DeviceService
}

func NewDeviceHandler(svc *service.DeviceService) *DeviceHandler {
	return &DeviceHandler{service: svc}
}

func (h *DeviceHandler) ListDevices(c *gin.Context) {
	userID := middleware.GetUserID(c)
	page, _ := strconv.Atoi(c.DefaultQuery("page", "1"))
	perPage, _ := strconv.Atoi(c.DefaultQuery("per_page", "20"))

	result, err := h.service.ListByUser(userID, page, perPage)
	if err != nil {
		log.Printf("[ListDevices] error: %v", err)
		response.Error(c, http.StatusInternalServerError, 50000, "获取设备列表失败")
		return
	}

	response.Success(c, result)
}

func (h *DeviceHandler) RemoveDevice(c *gin.Context) {
	userID := middleware.GetUserID(c)
	deviceID := c.Param("id")

	if err := h.service.RemoveDevice(userID, deviceID); err != nil {
		log.Printf("[RemoveDevice] error: %v", err)
		response.Error(c, http.StatusInternalServerError, 50000, "移除设备失败")
		return
	}

	response.SuccessWithMessage(c, "设备已移除", nil)
}

func (h *DeviceHandler) UpdateDevice(c *gin.Context) {
	userID := middleware.GetUserID(c)
	deviceID := c.Param("id")

	var req struct {
		DeviceName string `json:"device_name" binding:"required"`
	}
	if err := c.ShouldBindJSON(&req); err != nil {
		log.Printf("[UpdateDevice] invalid request: %v", err)
		response.Error(c, http.StatusBadRequest, 40000, "请求参数错误")
		return
	}

	if err := h.service.RenameDevice(userID, deviceID, req.DeviceName); err != nil {
		log.Printf("[UpdateDevice] error: %v", err)
		response.Error(c, http.StatusInternalServerError, 50000, "更新设备名称失败")
		return
	}

	response.SuccessWithMessage(c, "设备名称已更新", nil)
}
