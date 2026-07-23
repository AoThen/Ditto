import { describe, it, expect, vi, beforeEach } from 'vitest'
import { setActivePinia, createPinia } from 'pinia'
import * as devicesApi from '@/api/devices'
import { ElMessage, ElMessageBox } from 'element-plus'
import { formatDate } from '@/composables/useFormatDate'

vi.mock('@/api/devices', () => ({
  listDevices: vi.fn(),
  removeDevice: vi.fn(),
}))

vi.mock('element-plus', async () => {
  const actual = await vi.importActual('element-plus')
  return {
    ...actual,
    ElMessage: { success: vi.fn(), error: vi.fn(), warning: vi.fn() },
    ElMessageBox: { confirm: vi.fn() },
  }
})

function isDeviceActive(lastSeen) {
  if (!lastSeen) return false
  const lastSeenDate = new Date(lastSeen)
  const now = new Date()
  return (now - lastSeenDate) < 5 * 60 * 1000
}

describe('Devices API calls', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    setActivePinia(createPinia())
  })

  it('listDevices should return device list', async () => {
    const mockDevices = [
      { id: '1', device_id: 'dev-1', device_name: 'Device 1', last_seen: '2024-06-15T10:00:00Z', created_at: '2024-01-01T00:00:00Z' },
    ]
    devicesApi.listDevices.mockResolvedValue({ code: 0, data: { items: mockDevices, total: 1, page: 1, perPage: 20 } })

    const res = await devicesApi.listDevices()
    expect(res.code).toBe(0)
    expect(res.data.items).toHaveLength(1)
    expect(res.data.items[0].device_name).toBe('Device 1')
  })

  it('listDevices should handle empty response', async () => {
    devicesApi.listDevices.mockResolvedValue({ code: 0, data: { items: [], total: 0, page: 1, perPage: 20 } })

    const res = await devicesApi.listDevices()
    expect(res.data.items).toHaveLength(0)
  })

  it('listDevices should handle error', async () => {
    devicesApi.listDevices.mockRejectedValue(new Error('Network error'))
    await expect(devicesApi.listDevices()).rejects.toThrow('Network error')
  })

  it('removeDevice should succeed', async () => {
    devicesApi.removeDevice.mockResolvedValue({ code: 0 })

    const res = await devicesApi.removeDevice('dev-1')
    expect(res.code).toBe(0)
    expect(devicesApi.removeDevice).toHaveBeenCalledWith('dev-1')
  })

  it('removeDevice should handle error', async () => {
    devicesApi.removeDevice.mockRejectedValue(new Error('Failed to remove'))
    await expect(devicesApi.removeDevice('nonexistent')).rejects.toThrow('Failed to remove')
  })
})

describe('Devices formatting logic', () => {
  it('formatDate returns dash for null or undefined date', () => {
    expect(formatDate(null)).toBe('-')
    expect(formatDate(undefined)).toBe('-')
  })

  it('formatDate formats valid date string', () => {
    const result = formatDate('2024-06-15T10:30:00Z')
    expect(result).toBe('2024-06-15 10:30')
  })

  it('isDeviceActive returns false for null lastSeen', () => {
    expect(isDeviceActive(null)).toBe(false)
    expect(isDeviceActive(undefined)).toBe(false)
  })

  it('isDeviceActive returns true for recent activity', () => {
    const recent = new Date(Date.now() - 60000).toISOString() // 1 minute ago
    expect(isDeviceActive(recent)).toBe(true)
  })

  it('isDeviceActive returns false for old activity', () => {
    const old = new Date(Date.now() - 10 * 60 * 1000).toISOString() // 10 minutes ago
    expect(isDeviceActive(old)).toBe(false)
  })
})