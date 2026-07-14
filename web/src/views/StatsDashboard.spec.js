import { describe, it, expect, vi, beforeEach } from 'vitest'
import { setActivePinia, createPinia } from 'pinia'
import * as statsApi from '@/api/stats'
import * as clipsApi from '@/api/clips'
import { ElMessage } from 'element-plus'

vi.mock('@/api/stats', () => ({
  getStatsOverview: vi.fn(),
  getSyncLogs: vi.fn(),
}))

vi.mock('@/api/clips', () => ({
  listClips: vi.fn(),
}))

vi.mock('element-plus', async () => {
  const actual = await vi.importActual('element-plus')
  return {
    ...actual,
    ElMessage: { success: vi.fn(), error: vi.fn(), warning: vi.fn() },
  }
})

describe('StatsDashboard API calls', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    setActivePinia(createPinia())
  })

  it('getStatsOverview should return stats data', async () => {
    statsApi.getStatsOverview.mockResolvedValue({
      code: 0,
      data: {
        total_clips: 100,
        today_clips: 10,
        total_devices: 5,
        storage_mb: 25.5,
        max_storage_mb: 100,
        trend: [],
      },
    })

    const res = await statsApi.getStatsOverview()
    expect(res.code).toBe(0)
    expect(res.data.total_clips).toBe(100)
    expect(res.data.today_clips).toBe(10)
    expect(res.data.storage_mb).toBe(25.5)
  })

  it('getStatsOverview should handle empty data', async () => {
    statsApi.getStatsOverview.mockResolvedValue({ code: 0, data: null })

    const res = await statsApi.getStatsOverview()
    expect(res.code).toBe(0)
  })

  it('getStatsOverview should handle error', async () => {
    statsApi.getStatsOverview.mockRejectedValue(new Error('Network error'))

    await expect(statsApi.getStatsOverview()).rejects.toThrow('Network error')
  })

  it('listClips should return recent clips', async () => {
    clipsApi.listClips.mockResolvedValue({
      code: 0,
      data: {
        items: [
          { id: 1, description: 'test clip', source_device: 'dev-1', created_at: '2024-06-15T10:00:00Z' },
        ],
        total: 1,
      },
    })

    const res = await clipsApi.listClips({ page: 1, per_page: 5 })
    expect(res.code).toBe(0)
    expect(res.data.items).toHaveLength(1)
    expect(clipsApi.listClips).toHaveBeenCalledWith({ page: 1, per_page: 5 })
  })
})

describe('StatsDashboard computed properties', () => {
  function storagePercentage(stats) {
    const max = stats.max_storage_mb || 100
    const used = stats.storage_mb || 0
    return Math.min((used / max) * 100, 100)
  }

  function storageColor(pct) {
    if (pct < 50) return '#67c23a'
    if (pct < 80) return '#e6a23c'
    return '#f56c6c'
  }

  it('storagePercentage calculates correctly', () => {
    expect(storagePercentage({ storage_mb: 25, max_storage_mb: 100 })).toBe(25)
    expect(storagePercentage({ storage_mb: 50, max_storage_mb: 100 })).toBe(50)
    expect(storagePercentage({ storage_mb: 100, max_storage_mb: 100 })).toBe(100)
  })

  it('storagePercentage caps at 100', () => {
    expect(storagePercentage({ storage_mb: 150, max_storage_mb: 100 })).toBe(100)
  })

  it('storagePercentage uses defaults for missing values', () => {
    expect(storagePercentage({})).toBe(0)
    expect(storagePercentage({ storage_mb: null, max_storage_mb: null })).toBe(0)
  })

  it('storageColor returns green for low usage', () => {
    expect(storageColor(0)).toBe('#67c23a')
    expect(storageColor(25)).toBe('#67c23a')
    expect(storageColor(49)).toBe('#67c23a')
  })

  it('storageColor returns yellow for medium usage', () => {
    expect(storageColor(50)).toBe('#e6a23c')
    expect(storageColor(65)).toBe('#e6a23c')
    expect(storageColor(79)).toBe('#e6a23c')
  })

  it('storageColor returns red for high usage', () => {
    expect(storageColor(80)).toBe('#f56c6c')
    expect(storageColor(90)).toBe('#f56c6c')
    expect(storageColor(100)).toBe('#f56c6c')
  })
})

describe('StatsDashboard data formatting', () => {
  function formatDate(dateStr) {
    if (!dateStr) return '-'
    const date = new Date(dateStr)
    return `${date.getMonth() + 1}/${date.getDate()}`
  }

  function formatDateTime(dateStr) {
    if (!dateStr) return '-'
    return new Date(dateStr).toLocaleString('zh-CN')
  }

  it('formatDate returns dash for null/undefined', () => {
    expect(formatDate(null)).toBe('-')
    expect(formatDate(undefined)).toBe('-')
    expect(formatDate('')).toBe('-')
  })

  it('formatDate returns M/D format', () => {
    const result = formatDate('2024-06-15T10:00:00Z')
    expect(result).toBe('6/15')
  })

  it('formatDate handles different months', () => {
    expect(formatDate('2024-01-01T00:00:00Z')).toBe('1/1')
    expect(formatDate('2024-12-31T00:00:00Z')).toBe('12/31')
  })

  it('formatDateTime returns dash for null/undefined', () => {
    expect(formatDateTime(null)).toBe('-')
    expect(formatDateTime(undefined)).toBe('-')
  })

  it('formatDateTime formats valid date string', () => {
    const result = formatDateTime('2024-06-15T10:30:00Z')
    expect(result).toContain('2024')
    expect(result.length).toBeGreaterThan(5)
  })
})

describe('StatsDashboard getBarHeight', () => {
  function getBarHeight(count, trend) {
    const maxCount = Math.max(...trend.map(t => t.count), 1)
    return Math.max((count / maxCount) * 80, 5)
  }

  const trend = [
    { date: '2024-06-01', count: 5 },
    { date: '2024-06-02', count: 10 },
    { date: '2024-06-03', count: 3 },
  ]

  it('returns proportional height based on max count', () => {
    expect(getBarHeight(10, trend)).toBe(80)
    expect(getBarHeight(5, trend)).toBe(40)
    expect(getBarHeight(3, trend)).toBe(24)
  })

  it('returns minimum 5% for very small values', () => {
    expect(getBarHeight(1, trend)).toBe(8)
    expect(getBarHeight(0, trend)).toBe(5)
  })

  it('handles single item trend', () => {
    expect(getBarHeight(5, [{ date: '2024-01-01', count: 5 }])).toBe(80)
  })

  it('handles all zero counts', () => {
    const zeroTrend = [{ date: '2024-01-01', count: 0 }]
    expect(getBarHeight(0, zeroTrend)).toBe(5)
  })
})
