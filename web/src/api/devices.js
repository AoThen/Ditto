import request from '@/api/request'

// List user's devices
export function listDevices() {
  return request({
    url: '/api/v1/devices',
    method: 'get'
  })
}

// Remove a device
export function removeDevice(deviceId) {
  return request({
    url: `/api/v1/devices/${deviceId}`,
    method: 'delete'
  })
}
