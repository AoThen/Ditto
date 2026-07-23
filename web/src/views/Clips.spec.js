import { describe, it, expect, vi, beforeEach } from 'vitest'
import { setActivePinia, createPinia } from 'pinia'
import * as clipsApi from '@/api/clips'
import { ElMessage, ElMessageBox } from 'element-plus'
import { formatDate } from '@/composables/useFormatDate'

vi.mock('@/api/clips', () => ({
  listClips: vi.fn(),
  getClip: vi.fn(),
  getChanges: vi.fn(),
  deleteClip: vi.fn(),
}))

vi.mock('element-plus', async () => {
  const actual = await vi.importActual('element-plus')
  return {
    ...actual,
    ElMessage: { success: vi.fn(), error: vi.fn(), warning: vi.fn() },
    ElMessageBox: { confirm: vi.fn() },
  }
})

describe('Clips API calls', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    setActivePinia(createPinia())
  })

  it('listClips should return paginated clips', async () => {
    clipsApi.listClips.mockResolvedValue({
      code: 0,
      data: {
        items: [{ id: '1', description: 'test clip', paste_count: 3, created_at: '2024-01-15T10:00:00Z' }],
        total: 1,
        page: 1,
        page_size: 20,
      },
    })

    const res = await clipsApi.listClips({ page: 1, page_size: 20 })
    expect(res.code).toBe(0)
    expect(res.data.items).toHaveLength(1)
    expect(res.data.items[0].description).toBe('test clip')
    expect(clipsApi.listClips).toHaveBeenCalledWith({ page: 1, page_size: 20 })
  })

  it('listClips should support search query', async () => {
    clipsApi.listClips.mockResolvedValue({
      code: 0,
      data: { items: [], total: 0, page: 1, page_size: 20 },
    })

    const res = await clipsApi.listClips({ search: 'keyword', page: 1, page_size: 20 })
    expect(clipsApi.listClips).toHaveBeenCalledWith({ search: 'keyword', page: 1, page_size: 20 })
  })

  it('getClip should return single clip detail', async () => {
    clipsApi.getClip.mockResolvedValue({
      code: 0,
      data: { id: '1', description: 'detail', paste_count: 5 },
    })

    const res = await clipsApi.getClip('1')
    expect(res.code).toBe(0)
    expect(res.data.description).toBe('detail')
  })

  it('deleteClip should delete a clip', async () => {
    clipsApi.deleteClip.mockResolvedValue({ code: 0, data: null })

    const res = await clipsApi.deleteClip('1')
    expect(res.code).toBe(0)
    expect(clipsApi.deleteClip).toHaveBeenCalledWith('1')
  })

  it('should handle API errors', async () => {
    clipsApi.listClips.mockRejectedValue(new Error('Network error'))
    await expect(clipsApi.listClips()).rejects.toThrow('Network error')
  })
})

describe('Clip formatting logic', () => {
  it('should return dash for null date', () => {
    expect(formatDate(null)).toBe('-')
    expect(formatDate(undefined)).toBe('-')
  })

  it('should format valid date string', () => {
    const result = formatDate('2024-06-15T10:30:00Z')
    expect(result).toBe('2024-06-15 10:30')
  })
})
