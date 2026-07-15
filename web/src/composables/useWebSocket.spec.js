import { describe, it, expect, vi, beforeEach } from 'vitest'
import { setActivePinia, createPinia } from 'pinia'

function createMockWs() {
  const handlers = {}
  return {
    readyState: 1,
    send: vi.fn(),
    close: vi.fn(),
    addEventListener: vi.fn(),
    get onopen() { return handlers._onopen },
    set onopen(fn) { handlers._onopen = fn },
    get onmessage() { return handlers._onmessage },
    set onmessage(fn) { handlers._onmessage = fn },
    get onclose() { return handlers._onclose },
    set onclose(fn) { handlers._onclose = fn },
    get onerror() { return handlers._onerror },
    set onerror(fn) { handlers._onerror = fn },
    _handlers: handlers,
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
    document.cookie = 'device_id=test-device'

    mockWs = createMockWs()
    global.WebSocket = vi.fn(() => mockWs)

    mod = await import('@/composables/useWebSocket')
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
    global.WebSocket = vi.fn(() => mockWs)
    mod = await import('@/composables/useWebSocket')

    mod.useWebSocket().connect()
    expect(global.WebSocket).toHaveBeenCalledWith('https://example.com/api/v1/ws', ['cookie-auth'])
  })

  it('connect skips when not logged in', async () => {
    const userStore = (await import('@/stores/user')).useUserStore()
    userStore.isLoggedIn = false

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

  it('onmessage calls disconnect for goaway', () => {
    mod.useWebSocket().connect()

    mockWs.onmessage({ data: JSON.stringify({ type: 'goaway' }) })

    expect(global.WebSocket.mock.results[0].value.close).toHaveBeenCalled()
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
})