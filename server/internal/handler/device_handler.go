package handler

import (
	"net/http"

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

	devices, err := h.service.ListByUser(userID)
	if err != nil {
		response.Error(c, http.StatusInternalServerError, 50000, "获取设备列表失败: "+err.Error())
		return
	}

	response.Success(c, devices)
}

func (h *DeviceHandler) RemoveDevice(c *gin.Context) {
	userID := middleware.GetUserID(c)
	deviceID := c.Param("id")

	if err := h.service.RemoveDevice(userID, deviceID); err != nil {
		response.Error(c, http.StatusInternalServerError, 50000, "移除设备失败: "+err.Error())
		return
	}

	response.SuccessWithMessage(c, "设备已移除", nil)
}
