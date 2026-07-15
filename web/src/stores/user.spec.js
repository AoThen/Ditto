import { describe, it, expect, vi, beforeEach } from 'vitest'
import { setActivePinia, createPinia } from 'pinia'
import { useUserStore } from '@/stores/user'

describe('user store', () => {
  let store

  beforeEach(() => {
    setActivePinia(createPinia())
    document.cookie = 'device_id=; max-age=0; path=/'
    localStorage.clear()
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