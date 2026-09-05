import { describe, it, expect, vi, beforeEach } from 'vitest'
import { setActivePinia, createPinia } from 'pinia'
import { useUserStore } from '@/stores/user'
import { getMe } from '@/api/auth'

vi.mock('@/api/auth', () => ({
  getMe: vi.fn(),
}))

describe('user store', () => {
  let store

  beforeEach(() => {
    setActivePinia(createPinia())
    document.cookie.split(';').forEach(c => {
      const eq = c.indexOf('=')
      const name = eq > -1 ? c.substring(0, eq).trim() : c.trim()
      if (name) document.cookie = name + '=; expires=Thu, 01 Jan 1970 00:00:00 GMT'
    })
    localStorage.clear()
    vi.clearAllMocks()
    store = useUserStore()
  })

  it('initial state is logged out', () => {
    expect(store.isLoggedIn).toBe(false)
    expect(store.deviceId).toBe('')
    expect(store.username).toBe('')
  })

  it('checkAuthState detects device_id cookie', () => {
    document.cookie = 'device_id=dev-123'
    store.checkAuthState()
    expect(store.isLoggedIn).toBe(true)
    expect(store.deviceId).toBe('dev-123')
  })

  it('checkAuthState returns false when no cookie', () => {
    store.checkAuthState()
    expect(store.isLoggedIn).toBe(false)
    expect(store.deviceId).toBe('')
  })

  it('verifySession refreshes the identity from the server', async () => {
    store.setUserInfo({ device_id: 'old', username: 'old', role: 'user' })
    getMe.mockResolvedValue({ code: 0, data: { device_id: 'dev-9', username: 'newname', role: 'admin' } })

    await expect(store.verifySession()).resolves.toBe(true)
    expect(store.username).toBe('newname')
    expect(store.role).toBe('admin')
    expect(store.deviceId).toBe('dev-9')
    expect(JSON.parse(localStorage.getItem('userInfo')).username).toBe('newname')
  })

  it('verifySession clears the session when the server rejects', async () => {
    store.setUserInfo({ username: 'testuser' })
    getMe.mockResolvedValue({ code: 40100, message: '未提供认证令牌' })

    await expect(store.verifySession()).resolves.toBe(false)
    expect(store.isLoggedIn).toBe(false)
    expect(localStorage.getItem('userInfo')).toBeNull()
  })

  it('verifySession keeps the session when the API is unreachable', async () => {
    store.setUserInfo({ username: 'testuser' })
    getMe.mockRejectedValue(new Error('Network error'))

    await expect(store.verifySession()).resolves.toBe(false)
    expect(store.isLoggedIn).toBe(true)
  })

  it('verifySession skips the probe when not logged in locally', async () => {
    await expect(store.verifySession()).resolves.toBe(false)
    expect(getMe).not.toHaveBeenCalled()
  })

  it('setUserInfo updates state and localStorage', () => {
    store.setUserInfo({ username: 'testuser' })
    expect(store.username).toBe('testuser')
    expect(store.isLoggedIn).toBe(true)
    expect(localStorage.getItem('userInfo')).toBe(JSON.stringify({ username: 'testuser' }))
  })

  it('setUserInfo ignores null input', () => {
    store.setUserInfo(null)
    expect(store.isLoggedIn).toBe(false)
    expect(store.username).toBe('')
  })

  it('logout clears all state and dispatches event', () => {
    store.setUserInfo({ username: 'testuser' })
    document.cookie = 'device_id=dev-123; path=/'

    const events = []
    window.addEventListener('ws-disconnect', () => events.push('disconnected'))

    store.logout()

    expect(store.isLoggedIn).toBe(false)
    expect(store.deviceId).toBe('')
    expect(store.username).toBe('')
    expect(localStorage.getItem('userInfo')).toBeNull()
    expect(document.cookie).not.toContain('device_id')
    expect(events).toContain('disconnected')
  })
})