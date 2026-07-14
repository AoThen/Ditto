import { describe, it, expect, vi, beforeEach } from 'vitest'
import request from '@/api/request'
import { listConflictClips, resolveConflictClip } from './conflicts'

vi.mock('@/api/request', () => {
  const m = vi.fn(() => Promise.resolve({ code: 0, data: {} }))
  m.get = vi.fn(() => Promise.resolve({ code: 0, data: {} }))
  m.post = vi.fn(() => Promise.resolve({ code: 0, data: {} }))
  m.put = vi.fn(() => Promise.resolve({ code: 0, data: {} }))
  m.delete = vi.fn(() => Promise.resolve({ code: 0, data: {} }))
  return { default: m, downloadBlob: vi.fn() }
})

describe('conflicts API', () => {
  beforeEach(() => {
    vi.clearAllMocks()
  })

  it('listConflictClips should be a function', () => {
    expect(typeof listConflictClips).toBe('function')
  })

  it('listConflictClips should call GET /api/v1/clips/conflicts', async () => {
    await listConflictClips()
    expect(request).toHaveBeenCalledWith({ url: '/api/v1/clips/conflicts', method: 'get' })
  })

  it('resolveConflictClip should be a function', () => {
    expect(typeof resolveConflictClip).toBe('function')
  })

  it('resolveConflictClip should call POST /api/v1/clips/conflicts/:id/resolve with action', async () => {
    await resolveConflictClip('conflict-1', 'accept')
    expect(request).toHaveBeenCalledWith({
      url: '/api/v1/clips/conflicts/conflict-1/resolve',
      method: 'post',
      data: { action: 'accept' },
    })
  })
})
