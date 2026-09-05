import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { setActivePinia, createPinia } from 'pinia'

vi.mock('@/api/clips', () => ({
  getChanges: vi.fn(),
}))

function createMockWs() {
  return {
    readyState: 1,
    send: vi.fn(),
    close: vi.fn(),
    addEventListener: vi.fn(),
    onopen: vi.fn(),
    onmessage: vi.fn(),
    onclose: vi.fn(),
    onerror: vi.fn(),
  }
}

describe('useWebSocket', () => {
  let mockWs
  let mod

  beforeEach(async () => {
    vi.resetModules()
    vi.restoreAllMocks()
    setActivePinia(createPinia())
    vi.stubEnv('VITE_WS_URL', '')
    // The reconnect flag and the sync watermark live in localStorage: leaving
    // them set makes earlier tests trigger the reconnect catch-up path.
    localStorage.clear()
    vi.clearAllMocks()
    document.cookie = 'device_id=test-device'

    mockWs = createMockWs()
    const mockWsConstructor = vi.fn(function() { return mockWs })
    mockWsConstructor.OPEN = 1
    global.WebSocket = mockWsConstructor

    mod = await import('@/composables/useWebSocket')
  })

  afterEach(() => {
    mod.disconnectWebSocket()
  })

  it('connect creates a WebSocket connection with default URL', () => {
    const ws = mod.useWebSocket()
    ws.connect()

    expect(global.WebSocket).toHaveBeenCalledWith('/api/v1/ws', ['cookie-auth'])
    expect(mockWs.onopen).toBeDefined()
    expect(mockWs.onmessage).toBeDefined()
    expect(mockWs.onclose).toBeDefined()
    expect(mockWs.onerror).toBeDefined()
  })

  it('connect uses VITE_WS_URL when configured', async () => {
    vi.resetModules()
    vi.restoreAllMocks()
    setActivePinia(createPinia())
    vi.stubEnv('VITE_WS_URL', 'https://example.com')
    mockWs = createMockWs()
    const mockWsConstructor = vi.fn(function() { return mockWs })
    mockWsConstructor.OPEN = 1
    global.WebSocket = mockWsConstructor
    mod = await import('@/composables/useWebSocket')

    mod.useWebSocket().connect()
    expect(global.WebSocket).toHaveBeenCalledWith('https://example.com/api/v1/ws', ['cookie-auth'])
  })

  it('connect skips when not logged in', async () => {
    const userMod = await import('@/stores/user')
    vi.spyOn(userMod.useUserStore(), 'checkAuthState').mockImplementation(() => {})
    userMod.useUserStore().isLoggedIn = false

    const ws = mod.useWebSocket()
    ws.connect()

    expect(global.WebSocket).not.toHaveBeenCalled()
  })

  it('onopen sets isConnected to true', () => {
    const ws = mod.useWebSocket()
    ws.connect()
    expect(ws.isConnected.value).toBe(false)

    mockWs.onopen()

    expect(ws.isConnected.value).toBe(true)
  })

  it('onmessage handles ping messages', () => {
    mod.useWebSocket().connect()

    const consoleSpy = vi.spyOn(console, 'warn')
    mockWs.onmessage({ data: JSON.stringify({ type: 'ping' }) })
    expect(consoleSpy).not.toHaveBeenCalled()
  })

  it('onmessage dispatches ws-clip-added event for clip_added', () => {
    mod.useWebSocket().connect()

    const events = []
    window.addEventListener('ws-clip-added', (e) => events.push(e.detail))

    mockWs.onmessage({ data: JSON.stringify({ type: 'clip_added', data: { id: 'clip-1' } }) })

    expect(events).toHaveLength(1)
    expect(events[0].id).toBe('clip-1')
  })

  it('onmessage dispatches ws-clip-added for each clip in clips_added', () => {
    mod.useWebSocket().connect()

    const events = []
    window.addEventListener('ws-clip-added', (e) => events.push(e.detail))

    mockWs.onmessage({ data: JSON.stringify({ type: 'clips_added', data: { clips: [{ id: 'clip-1' }, { id: 'clip-2' }] } }) })

    expect(events).toHaveLength(2)
    expect(events[0].id).toBe('clip-1')
    expect(events[1].id).toBe('clip-2')
  })

  it('handles malformed clips_added message gracefully', () => {
    mod.useWebSocket().connect()
    // Should not throw when data is null
    expect(() => {
      mockWs.onmessage({ data: JSON.stringify({ type: 'clips_added', data: null }) })
    }).not.toThrow()
    // Should not throw when clips is missing
    expect(() => {
      mockWs.onmessage({ data: JSON.stringify({ type: 'clips_added', data: {} }) })
    }).not.toThrow()
  })

  it('onmessage calls disconnect for goaway', () => {
    mod.useWebSocket().connect()

    mockWs.onmessage({ data: JSON.stringify({ type: 'goaway' }) })

    expect(mockWs.close).toHaveBeenCalled()
  })

  it('onclose with code 1000 does not schedule reconnect', () => {
    vi.useFakeTimers()
    mod.useWebSocket().connect()

    mockWs.onclose({ code: 1000 })

    vi.advanceTimersByTime(5000)
    expect(global.WebSocket).toHaveBeenCalledTimes(1)
    vi.useRealTimers()
  })

  it('onclose with abnormal code does not reconnect when no consumers', () => {
    vi.useFakeTimers()
    mod.useWebSocket().connect()

    mockWs.onclose({ code: 1006, reason: 'Abnormal' })

    vi.advanceTimersByTime(5000)
    expect(global.WebSocket).toHaveBeenCalledTimes(1)
    vi.useRealTimers()
  })

  it('onerror does not throw', () => {
    mod.useWebSocket().connect()
    expect(() => mockWs.onerror({ message: 'test error' })).not.toThrow()
  })

  it('disconnectExplicit closes WebSocket and clears timers', () => {
    vi.useFakeTimers()
    const ws = mod.useWebSocket()
    ws.connect()

    ws.disconnect()

    expect(mockWs.close).toHaveBeenCalledWith(1000, 'Client disconnect')
    expect(ws.isConnected.value).toBe(false)
    vi.useRealTimers()
  })

  it('isConnectedToServer returns correct state', () => {
    const ws = mod.useWebSocket()
    expect(ws.isConnectedToServer()).toBe(false)

    ws.connect()

    mockWs.onopen()
    expect(ws.isConnectedToServer()).toBe(true)

    ws.disconnect()
    expect(ws.isConnectedToServer()).toBe(false)
  })

  it('connect does nothing if already connected', () => {
    const ws = mod.useWebSocket()
    ws.connect()
    mockWs.onopen()

    ws.connect()

    expect(global.WebSocket).toHaveBeenCalledTimes(1)
  })

  it('disconnectWebSocket is exported as alias', () => {
    expect(mod.disconnectWebSocket).toBeDefined()
    expect(typeof mod.disconnectWebSocket).toBe('function')
  })

  it('ignores clips_added originating from own device', () => {
    mod.useWebSocket().connect()

    const events = []
    window.addEventListener('ws-clip-added', (e) => events.push(e.detail))

    mockWs.onmessage({ data: JSON.stringify({ type: 'clips_added', data: { clips: [
      { id: 'own', device_id: 'test-device' },
      { id: 'other', device_id: 'other-device' },
    ] } }) })

    expect(events).toHaveLength(1)
    expect(events[0].id).toBe('other')
  })

  it('ignores clips_deleted originating from own device', () => {
    mod.useWebSocket().connect()

    const events = []
    window.addEventListener('ws-clips-deleted', (e) => events.push(e.detail))

    mockWs.onmessage({ data: JSON.stringify({ type: 'clips_deleted', data: { clip_ids: ['own'], device_id: 'test-device' } }) })
    expect(events).toHaveLength(0)

    mockWs.onmessage({ data: JSON.stringify({ type: 'clips_deleted', data: { clip_ids: ['other'], device_id: 'other-device' } }) })
    expect(events).toHaveLength(1)
    expect(events[0]).toEqual(['other'])
  })

  it('catch-up drains every page before advancing the sync watermark', async () => {
    const { getChanges } = await import('@/api/clips')
    const { useClipStore } = await import('@/stores/clip')
    const clipStore = useClipStore()
    clipStore.updateSyncTime('2024-01-01T00:00:00Z')
    getChanges.mockImplementation(async (since, page) => page === 1
      ? { code: 0, data: { clips: [], deleted_ids: [], has_more: true, server_time: '2024-02-01T00:00:00Z' } }
      : { code: 0, data: { clips: [], deleted_ids: [], has_more: false, server_time: '2024-02-02T00:00:00Z' } })

    localStorage.setItem('ditto_ws_reconnecting', '1')
    mod.useWebSocket().connect()
    mockWs.onopen()

    await vi.waitFor(() => expect(getChanges).toHaveBeenCalledTimes(2))
    expect(clipStore.lastSyncTime).toBe('2024-02-02T00:00:00Z')
    expect(localStorage.getItem('ditto_last_sync_time')).toBe('2024-02-02T00:00:00Z')
  })

  it('catch-up leaves the watermark alone when a page fails', async () => {
    const { getChanges } = await import('@/api/clips')
    const { useClipStore } = await import('@/stores/clip')
    const clipStore = useClipStore()
    clipStore.updateSyncTime('2024-01-01T00:00:00Z')
    getChanges.mockImplementation(async (since, page) => page === 1
      ? { code: 0, data: { clips: [], deleted_ids: [], has_more: true, server_time: '2024-02-01T00:00:00Z' } }
      : { code: 50000, message: 'boom' })

    localStorage.setItem('ditto_ws_reconnecting', '1')
    mod.useWebSocket().connect()
    mockWs.onopen()

    await vi.waitFor(() => expect(getChanges).toHaveBeenCalledTimes(2))
    expect(clipStore.lastSyncTime).toBe('2024-01-01T00:00:00Z')
  })
})