import { describe, it, expect, vi, beforeEach } from 'vitest'
import request from '@/api/request'
import { getStatsOverview, getSyncLogs } from './stats'

vi.mock('@/api/request', () => {
  const m = vi.fn(() => Promise.resolve({ code: 0, data: {} }))
  m.get = vi.fn(() => Promise.resolve({ code: 0, data: {} }))
  m.post = vi.fn(() => Promise.resolve({ code: 0, data: {} }))
  m.put = vi.fn(() => Promise.resolve({ code: 0, data: {} }))
  m.delete = vi.fn(() => Promise.resolve({ code: 0, data: {} }))
  return { default: m, downloadBlob: vi.fn() }
})

describe('stats API', () => {
  beforeEach(() => {
    vi.clearAllMocks()
  })

  it('getStatsOverview should be a function', () => {
    expect(typeof getStatsOverview).toBe('function')
  })

  it('getStatsOverview should call GET /api/v1/stats/overview', async () => {
    await getStatsOverview()
    expect(request).toHaveBeenCalledWith({ url: '/api/v1/stats/overview', method: 'get' })
  })

  it('getSyncLogs should be a function', () => {
    expect(typeof getSyncLogs).toBe('function')
  })

  it('getSyncLogs should call GET /api/v1/stats/sync-logs with params', async () => {
    const params = { page: 1, page_size: 10 }
    await getSyncLogs(params)
    expect(request).toHaveBeenCalledWith({ url: '/api/v1/stats/sync-logs', method: 'get', params })
  })

  it('getSyncLogs should work without params', async () => {
    await getSyncLogs()
    expect(request).toHaveBeenCalledWith({ url: '/api/v1/stats/sync-logs', method: 'get', params: {} })
  })
})
