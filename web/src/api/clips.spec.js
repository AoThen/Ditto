import { describe, it, expect, vi, beforeEach } from 'vitest'
import request from '@/api/request'
import {
  listClips, getClip, getChanges, deleteClip,
  getEncryptionSalt, getKeyMaterial,
  setupEncryption, disableEncryption, changeEncryptionPassword,
} from './clips'

vi.mock('@/api/request', () => {
  const m = vi.fn(() => Promise.resolve({ code: 0, data: {} }))
  m.get = vi.fn(() => Promise.resolve({ code: 0, data: {} }))
  m.post = vi.fn(() => Promise.resolve({ code: 0, data: {} }))
  m.put = vi.fn(() => Promise.resolve({ code: 0, data: {} }))
  m.delete = vi.fn(() => Promise.resolve({ code: 0, data: {} }))
  return { default: m, downloadBlob: vi.fn() }
})

describe('clips API', () => {
  beforeEach(() => {
    vi.clearAllMocks()
  })

  it('listClips should be a function', () => {
    expect(typeof listClips).toBe('function')
  })

  it('listClips should call GET /api/v1/clips with params', async () => {
    await listClips({ page: 1, page_size: 20 })
    expect(request).toHaveBeenCalledWith({ url: '/api/v1/clips', method: 'get', params: { page: 1, page_size: 20 } })
  })

  it('listClips should work without params', async () => {
    await listClips()
    expect(request).toHaveBeenCalledWith({ url: '/api/v1/clips', method: 'get', params: {} })
  })

  it('getClip should be a function', () => {
    expect(typeof getClip).toBe('function')
  })

  it('getClip should call GET /api/v1/clips/:id', async () => {
    await getClip('123')
    expect(request).toHaveBeenCalledWith({ url: '/api/v1/clips/123', method: 'get' })
  })

  it('getChanges should be a function', () => {
    expect(typeof getChanges).toBe('function')
  })

  it('getChanges should call GET /api/v1/clips/changes with since', async () => {
    const since = '2024-01-01T00:00:00Z'
    await getChanges(since)
    expect(request).toHaveBeenCalledWith({ url: '/api/v1/clips/changes', method: 'get', params: { since } })
  })

  it('getChanges should omit page/limit on the first page when no limit is given', async () => {
    await getChanges('2024-01-01T00:00:00Z', 1)
    expect(request).toHaveBeenCalledWith({
      url: '/api/v1/clips/changes',
      method: 'get',
      params: { since: '2024-01-01T00:00:00Z' }
    })
  })

  it('getChanges should send page and limit for subsequent pages', async () => {
    await getChanges('2024-01-01T00:00:00Z', 3, 1000)
    expect(request).toHaveBeenCalledWith({
      url: '/api/v1/clips/changes',
      method: 'get',
      params: { since: '2024-01-01T00:00:00Z', page: 3, limit: 1000 }
    })
  })

  it('deleteClip should be a function', () => {
    expect(typeof deleteClip).toBe('function')
  })

  it('deleteClip should call DELETE /api/v1/clips/:id', async () => {
    await deleteClip('456')
    expect(request).toHaveBeenCalledWith({ url: '/api/v1/clips/456', method: 'delete' })
  })

  it('getEncryptionSalt should be a function', () => {
    expect(typeof getEncryptionSalt).toBe('function')
  })

  it('getEncryptionSalt should call GET /api/v1/encryption/salt', async () => {
    await getEncryptionSalt()
    expect(request).toHaveBeenCalledWith({ url: '/api/v1/encryption/salt', method: 'get' })
  })

  it('getKeyMaterial should be a function', () => {
    expect(typeof getKeyMaterial).toBe('function')
  })

  it('getKeyMaterial should call GET /api/v1/encryption/key-material', async () => {
    await getKeyMaterial()
    expect(request).toHaveBeenCalledWith({ url: '/api/v1/encryption/key-material', method: 'get' })
  })

  it('setupEncryption should be a function', () => {
    expect(typeof setupEncryption).toBe('function')
  })

  it('setupEncryption should call POST /api/v1/encryption/setup with data', async () => {
    const data = { password: 'secret' }
    await setupEncryption(data)
    expect(request).toHaveBeenCalledWith({ url: '/api/v1/encryption/setup', method: 'post', data })
  })

  it('disableEncryption should be a function', () => {
    expect(typeof disableEncryption).toBe('function')
  })

  it('disableEncryption should call POST /api/v1/encryption/disable', async () => {
    await disableEncryption()
    expect(request).toHaveBeenCalledWith({ url: '/api/v1/encryption/disable', method: 'post' })
  })

  it('changeEncryptionPassword should be a function', () => {
    expect(typeof changeEncryptionPassword).toBe('function')
  })

  it('changeEncryptionPassword should call POST /api/v1/encryption/change-password with data', async () => {
    const data = { old_password: 'old', new_password: 'new' }
    await changeEncryptionPassword(data)
    expect(request).toHaveBeenCalledWith({ url: '/api/v1/encryption/change-password', method: 'post', data })
  })
})
