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
