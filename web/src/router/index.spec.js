import { createRouter, createWebHistory } from 'vue-router'
import { routes, beforeEachGuard } from '@/router'

vi.mock('@/stores/user', () => ({ useUserStore: vi.fn() }))

describe('router guards', () => {
  let router
  let userStore

  beforeEach(async () => {
    userStore = { isLoggedIn: false, role: 'user' }
    const { useUserStore } = await import('@/stores/user')
    useUserStore.mockReturnValue(userStore)
    router = createRouter({ history: createWebHistory(), routes })
    router.beforeEach(beforeEachGuard)
    router.push('/')
    await router.isReady()
  })

  it('redirects unauthenticated user to /login', async () => {
    await router.push('/dashboard/clips')
    expect(router.currentRoute.value.path).toBe('/login')
  })

  it('redirects authenticated user away from /login to /dashboard', async () => {
    userStore.isLoggedIn = true
    await router.push('/dashboard/clips')
    await router.push('/login')
    expect(router.currentRoute.value.path).toBe('/dashboard')
  })

  it('redirects non-admin user away from /dashboard/admin/users to /dashboard', async () => {
    userStore.isLoggedIn = true
    userStore.role = 'user'
    await router.push('/dashboard/admin/users')
    expect(router.currentRoute.value.path).toBe('/dashboard')
  })

  it('allows admin user to access /dashboard/admin/users', async () => {
    userStore.isLoggedIn = true
    userStore.role = 'admin'
    await router.push('/dashboard/admin/users')
    expect(router.currentRoute.value.path).toBe('/dashboard/admin/users')
  })
})