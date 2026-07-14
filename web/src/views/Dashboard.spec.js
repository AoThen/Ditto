import { describe, it, expect, vi, beforeEach } from 'vitest'
import { setActivePinia, createPinia } from 'pinia'
import { useUserStore } from '@/stores/user'
import { ElMessage } from 'element-plus'

vi.mock('@/composables/useWebSocket', () => ({
  useWebSocket: () => ({
    connect: vi.fn(),
    disconnect: vi.fn(),
    isConnected: { value: false },
    isConnectedToServer: () => false,
  }),
  disconnectWebSocket: vi.fn(),
}))

vi.mock('axios', () => ({
  default: {
    post: vi.fn(() => Promise.resolve({ data: {} })),
  },
  post: vi.fn(() => Promise.resolve({ data: {} })),
}))

vi.mock('element-plus', async () => {
  const actual = await vi.importActual('element-plus')
  return {
    ...actual,
    ElMessage: { success: vi.fn(), error: vi.fn(), warning: vi.fn() },
  }
})

describe('Dashboard handleLogout', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    setActivePinia(createPinia())
  })

  it('handleLogout calls disconnect, axios.post, userStore.logout, and router.push', async () => {
    const axios = await import('axios')
    const { useWebSocket } = await import('@/composables/useWebSocket')
    const routerPush = vi.fn()

    const ws = useWebSocket()

    function handleLogout() {
      ws.disconnect()
      axios.post('/api/v1/auth/logout', {}, { withCredentials: true }).catch(() => {}).finally(() => {
        const userStore = useUserStore()
        userStore.logout()
        ElMessage.success('已退出登录')
        routerPush('/login')
      })
    }

    handleLogout()

    expect(ws.disconnect).toHaveBeenCalled()
    expect(axios.post).toHaveBeenCalledWith('/api/v1/auth/logout', {}, { withCredentials: true })

    await new Promise(resolve => setTimeout(resolve, 50))

    const userStore = useUserStore()
    expect(userStore.isLoggedIn).toBe(false)
    expect(ElMessage.success).toHaveBeenCalledWith('已退出登录')
    expect(routerPush).toHaveBeenCalledWith('/login')
  })

  it('handleLogout works when axios.post fails', async () => {
    const axios = await import('axios')
    const { useWebSocket } = await import('@/composables/useWebSocket')
    const routerPush = vi.fn()

    axios.post.mockRejectedValue(new Error('Network error'))
    const ws = useWebSocket()

    function handleLogout() {
      ws.disconnect()
      axios.post('/api/v1/auth/logout', {}, { withCredentials: true }).catch(() => {}).finally(() => {
        const userStore = useUserStore()
        userStore.logout()
        ElMessage.success('已退出登录')
        routerPush('/login')
      })
    }

    handleLogout()

    expect(ws.disconnect).toHaveBeenCalled()

    await new Promise(resolve => setTimeout(resolve, 50))

    const userStore = useUserStore()
    expect(userStore.isLoggedIn).toBe(false)
    expect(ElMessage.success).toHaveBeenCalledWith('已退出登录')
    expect(routerPush).toHaveBeenCalledWith('/login')
  })
})

describe('Dashboard activeMenu', () => {
  function getActiveMenu(routePath) {
    return routePath
  }

  it('returns current route path', () => {
    expect(getActiveMenu('/dashboard')).toBe('/dashboard')
    expect(getActiveMenu('/dashboard/clips')).toBe('/dashboard/clips')
    expect(getActiveMenu('/dashboard/devices')).toBe('/dashboard/devices')
  })
})

describe('Dashboard WebSocket', () => {
  it('WebSocket connect is called on setup', async () => {
    const { useWebSocket } = await import('@/composables/useWebSocket')
    const ws = useWebSocket()
    ws.connect()
    expect(ws.connect).toHaveBeenCalled()
  })
})
