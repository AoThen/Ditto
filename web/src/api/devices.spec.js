import { describe, it, expect, vi, beforeEach } from 'vitest'
import request from '@/api/request'
import { listDevices, removeDevice } from './devices'

vi.mock('@/api/request', () => {
  const m = vi.fn(() => Promise.resolve({ code: 0, data: {} }))
  m.get = vi.fn(() => Promise.resolve({ code: 0, data: {} }))
  m.post = vi.fn(() => Promise.resolve({ code: 0, data: {} }))
  m.put = vi.fn(() => Promise.resolve({ code: 0, data: {} }))
  m.delete = vi.fn(() => Promise.resolve({ code: 0, data: {} }))
  return { default: m, downloadBlob: vi.fn() }
})

describe('devices API', () => {
  beforeEach(() => {
    vi.clearAllMocks()
  })

  it('listDevices should be a function', () => {
    expect(typeof listDevices).toBe('function')
  })

  it('listDevices should call GET /api/v1/devices', async () => {
    await listDevices()
    expect(request).toHaveBeenCalledWith({ url: '/api/v1/devices', method: 'get' })
  })

  it('removeDevice should be a function', () => {
    expect(typeof removeDevice).toBe('function')
  })

  it('removeDevice should call DELETE /api/v1/devices/:id', async () => {
    await removeDevice('dev-789')
    expect(request).toHaveBeenCalledWith({ url: '/api/v1/devices/dev-789', method: 'delete' })
  })
})
