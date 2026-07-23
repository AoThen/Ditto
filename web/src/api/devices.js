import request from '@/api/request'

// List user's devices
export function listDevices(page = 1, perPage = 20) {
  return request({
    url: '/api/v1/devices',
    method: 'get',
    params: { page, per_page: perPage }
  })
}

// Remove a device
export function removeDevice(deviceId) {
  return request({
    url: `/api/v1/devices/${deviceId}`,
    method: 'delete'
  })
}

// Rename a device
export function renameDevice(deviceId, deviceName) {
  return request({
    url: `/api/v1/devices/${deviceId}`,
    method: 'patch',
    data: { device_name: deviceName }
  })
}
