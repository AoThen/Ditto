import { describe, it, expect, vi, beforeEach } from 'vitest'
import { login, register } from './auth'
import request from '@/api/request'

vi.mock('@/api/request', () => {
  const m = vi.fn(() => Promise.resolve({ code: 0, data: {} }))
  m.get = vi.fn(() => Promise.resolve({ code: 0, data: {} }))
  m.post = vi.fn(() => Promise.resolve({ code: 0, data: {} }))
  m.put = vi.fn(() => Promise.resolve({ code: 0, data: {} }))
  m.delete = vi.fn(() => Promise.resolve({ code: 0, data: {} }))
  return { default: m, downloadBlob: vi.fn() }
})

describe('auth API', () => {
  beforeEach(() => {
    vi.clearAllMocks()
  })

  it('login should be a function', () => {
    expect(typeof login).toBe('function')
  })

  it('register should be a function', () => {
    expect(typeof register).toBe('function')
  })

  it('login should call POST /api/v1/auth/login with data', async () => {
    const data = { username: 'testuser', password: 'testpass' }
    await login(data)
    expect(request.post).toHaveBeenCalledWith('/api/v1/auth/login', data)
  })

  it('register should call POST /api/v1/auth/register with data', async () => {
    const data = { username: 'newuser', password: 'newpass' }
    await register(data)
    expect(request.post).toHaveBeenCalledWith('/api/v1/auth/register', data)
  })

  it('login should return data on success', async () => {
    const mockResponse = { code: 0, data: { device_id: 'dev-123' }, message: '登录成功' }
    request.post.mockResolvedValue(mockResponse)
    const res = await login({ username: 'test', password: 'pass' })
    expect(res).toEqual(mockResponse)
  })

  it('register should return data on success', async () => {
    const mockResponse = { code: 0, data: null, message: '注册成功' }
    request.post.mockResolvedValue(mockResponse)
    const res = await register({ username: 'new', password: 'pass' })
    expect(res).toEqual(mockResponse)
  })

  it('login should handle error', async () => {
    request.post.mockRejectedValue(new Error('用户名或密码错误'))
    await expect(login({ username: 'bad', password: 'wrong' })).rejects.toThrow('用户名或密码错误')
  })

  it('register should handle error', async () => {
    request.post.mockRejectedValue(new Error('Network error'))
    await expect(register({})).rejects.toThrow('Network error')
  })
})
