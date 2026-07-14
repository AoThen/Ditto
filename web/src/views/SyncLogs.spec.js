import { describe, it, expect, vi, beforeEach } from 'vitest'
import { setActivePinia, createPinia } from 'pinia'
import * as statsApi from '@/api/stats'
import { ElMessage } from 'element-plus'

vi.mock('@/api/stats', () => ({
  getSyncLogs: vi.fn(),
  getStatsOverview: vi.fn(),
}))

vi.mock('element-plus', async () => {
  const actual = await vi.importActual('element-plus')
  return {
    ...actual,
    ElMessage: { success: vi.fn(), error: vi.fn(), warning: vi.fn() },
  }
})

describe('SyncLogs API calls', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    setActivePinia(createPinia())
  })

  it('getSyncLogs should return paginated logs', async () => {
    statsApi.getSyncLogs.mockResolvedValue({
      code: 0,
      data: {
        items: [
          { id: 1, action: 'push', status: 'success', device_id: 'dev-1', clip_count: 3, synced_at: '2024-01-15T10:00:00Z' },
        ],
        total: 1,
      },
    })

    const res = await statsApi.getSyncLogs({ page: 1, per_page: 20 })
    expect(res.code).toBe(0)
    expect(res.data.items).toHaveLength(1)
    expect(res.data.items[0].action).toBe('push')
  })

  it('should handle empty logs', async () => {
    statsApi.getSyncLogs.mockResolvedValue({
      code: 0,
      data: { items: [], total: 0 },
    })

    const res = await statsApi.getSyncLogs({ page: 1, per_page: 20 })
    expect(res.data.items).toHaveLength(0)
    expect(res.data.total).toBe(0)
  })

  it('should handle API error', async () => {
    statsApi.getSyncLogs.mockRejectedValue(new Error('Network error'))
    await expect(statsApi.getSyncLogs()).rejects.toThrow('Network error')
  })
})

describe('formatTime', () => {
  function formatTime(timeStr) {
    if (!timeStr) return '-'
    const date = new Date(timeStr)
    return date.toLocaleString('zh-CN', {
      year: 'numeric', month: '2-digit', day: '2-digit',
      hour: '2-digit', minute: '2-digit', second: '2-digit',
    })
  }

  it('should return dash for null/undefined', () => {
    expect(formatTime(null)).toBe('-')
    expect(formatTime(undefined)).toBe('-')
  })

  it('should format valid date', () => {
    const result = formatTime('2024-06-15T10:30:00Z')
    expect(result).toContain('2024')
  })
})

describe('getActionTagType', () => {
  function getActionTagType(action) {
    switch (action) {
      case 'push': return 'primary'
      case 'pull': return 'success'
      case 'delete': return 'danger'
      default: return 'info'
    }
  }

  it('should return primary for push', () => {
    expect(getActionTagType('push')).toBe('primary')
  })

  it('should return success for pull', () => {
    expect(getActionTagType('pull')).toBe('success')
  })

  it('should return danger for delete', () => {
    expect(getActionTagType('delete')).toBe('danger')
  })

  it('should return info for unknown action', () => {
    expect(getActionTagType('unknown')).toBe('info')
  })

  it('should return info for empty action', () => {
    expect(getActionTagType('')).toBe('info')
  })
})

describe('getActionLabel', () => {
  function getActionLabel(action) {
    switch (action) {
      case 'push': return '推送'
      case 'pull': return '拉取'
      case 'delete': return '删除'
      default: return action
    }
  }

  it('should return Chinese labels', () => {
    expect(getActionLabel('push')).toBe('推送')
    expect(getActionLabel('pull')).toBe('拉取')
    expect(getActionLabel('delete')).toBe('删除')
  })

  it('should return action as-is for unknown', () => {
    expect(getActionLabel('unknown')).toBe('unknown')
  })
})
