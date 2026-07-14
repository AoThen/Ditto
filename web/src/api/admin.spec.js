import { describe, it, expect, vi, beforeEach } from 'vitest'
import request from '@/api/request'
import { getUsers, getUser, createUser, updateUser, deleteUser, resetPassword } from './admin'

vi.mock('@/api/request', () => {
  const m = vi.fn(() => Promise.resolve({ code: 0, data: {} }))
  m.get = vi.fn(() => Promise.resolve({ code: 0, data: {} }))
  m.post = vi.fn(() => Promise.resolve({ code: 0, data: {} }))
  m.put = vi.fn(() => Promise.resolve({ code: 0, data: {} }))
  m.delete = vi.fn(() => Promise.resolve({ code: 0, data: {} }))
  return { default: m, downloadBlob: vi.fn() }
})

describe('admin API', () => {
  beforeEach(() => {
    vi.clearAllMocks()
  })

  it('getUsers should be a function', () => {
    expect(typeof getUsers).toBe('function')
  })

  it('getUsers should call GET /api/v1/admin/users with params', async () => {
    const params = { page: 1, page_size: 10 }
    await getUsers(params)
    expect(request.get).toHaveBeenCalledWith('/api/v1/admin/users', { params })
  })

  it('getUser should be a function', () => {
    expect(typeof getUser).toBe('function')
  })

  it('getUser should call GET /api/v1/admin/users/:id', async () => {
    await getUser('u-1')
    expect(request.get).toHaveBeenCalledWith('/api/v1/admin/users/u-1')
  })

  it('createUser should be a function', () => {
    expect(typeof createUser).toBe('function')
  })

  it('createUser should call POST /api/v1/admin/users with data', async () => {
    const data = { username: 'newuser', password: 'pass', role: 'user' }
    await createUser(data)
    expect(request.post).toHaveBeenCalledWith('/api/v1/admin/users', data)
  })

  it('updateUser should be a function', () => {
    expect(typeof updateUser).toBe('function')
  })

  it('updateUser should call PUT /api/v1/admin/users/:id with data', async () => {
    const data = { role: 'admin' }
    await updateUser('u-2', data)
    expect(request.put).toHaveBeenCalledWith('/api/v1/admin/users/u-2', data)
  })

  it('deleteUser should be a function', () => {
    expect(typeof deleteUser).toBe('function')
  })

  it('deleteUser should call DELETE /api/v1/admin/users/:id', async () => {
    await deleteUser('u-3')
    expect(request.delete).toHaveBeenCalledWith('/api/v1/admin/users/u-3')
  })

  it('resetPassword should be a function', () => {
    expect(typeof resetPassword).toBe('function')
  })

  it('resetPassword should call POST /api/v1/admin/users/:id/reset-password with data', async () => {
    const data = { new_password: 'newpass' }
    await resetPassword('u-4', data)
    expect(request.post).toHaveBeenCalledWith('/api/v1/admin/users/u-4/reset-password', data)
  })
})
