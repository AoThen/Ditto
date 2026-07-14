import { describe, it, expect, vi, beforeEach } from 'vitest'
import request from '@/api/request'
import {
  listGroups, getGroup, createGroup, updateGroup, deleteGroup,
  moveClipsToGroup, removeClipsFromGroup,
} from './groups'

vi.mock('@/api/request', () => {
  const m = vi.fn(() => Promise.resolve({ code: 0, data: {} }))
  m.get = vi.fn(() => Promise.resolve({ code: 0, data: {} }))
  m.post = vi.fn(() => Promise.resolve({ code: 0, data: {} }))
  m.put = vi.fn(() => Promise.resolve({ code: 0, data: {} }))
  m.delete = vi.fn(() => Promise.resolve({ code: 0, data: {} }))
  return { default: m, downloadBlob: vi.fn() }
})

describe('groups API', () => {
  beforeEach(() => {
    vi.clearAllMocks()
  })

  it('listGroups should be a function', () => {
    expect(typeof listGroups).toBe('function')
  })

  it('listGroups should call GET /api/v1/groups', async () => {
    await listGroups()
    expect(request).toHaveBeenCalledWith({ url: '/api/v1/groups', method: 'get' })
  })

  it('getGroup should be a function', () => {
    expect(typeof getGroup).toBe('function')
  })

  it('getGroup should call GET /api/v1/groups/:id', async () => {
    await getGroup('g-1')
    expect(request).toHaveBeenCalledWith({ url: '/api/v1/groups/g-1', method: 'get' })
  })

  it('createGroup should be a function', () => {
    expect(typeof createGroup).toBe('function')
  })

  it('createGroup should call POST /api/v1/groups with data', async () => {
    const data = { name: 'New Group', color: '#ff0000' }
    await createGroup(data)
    expect(request).toHaveBeenCalledWith({ url: '/api/v1/groups', method: 'post', data })
  })

  it('updateGroup should be a function', () => {
    expect(typeof updateGroup).toBe('function')
  })

  it('updateGroup should call PUT /api/v1/groups/:id with data', async () => {
    const data = { name: 'Updated Group' }
    await updateGroup('g-2', data)
    expect(request).toHaveBeenCalledWith({ url: '/api/v1/groups/g-2', method: 'put', data })
  })

  it('deleteGroup should be a function', () => {
    expect(typeof deleteGroup).toBe('function')
  })

  it('deleteGroup should call DELETE /api/v1/groups/:id', async () => {
    await deleteGroup('g-3')
    expect(request).toHaveBeenCalledWith({ url: '/api/v1/groups/g-3', method: 'delete' })
  })

  it('moveClipsToGroup should be a function', () => {
    expect(typeof moveClipsToGroup).toBe('function')
  })

  it('moveClipsToGroup should call POST /api/v1/groups/:id/move-clips with clip_ids', async () => {
    await moveClipsToGroup('g-1', ['clip-1', 'clip-2'])
    expect(request).toHaveBeenCalledWith({
      url: '/api/v1/groups/g-1/move-clips',
      method: 'post',
      data: { clip_ids: ['clip-1', 'clip-2'] },
    })
  })

  it('removeClipsFromGroup should be a function', () => {
    expect(typeof removeClipsFromGroup).toBe('function')
  })

  it('removeClipsFromGroup should call POST /api/v1/clips/remove-from-group with clip_ids', async () => {
    await removeClipsFromGroup(['clip-3', 'clip-4'])
    expect(request).toHaveBeenCalledWith({
      url: '/api/v1/clips/remove-from-group',
      method: 'post',
      data: { clip_ids: ['clip-3', 'clip-4'] },
    })
  })
})
