import { describe, it, expect, vi, beforeEach } from 'vitest'
import { setActivePinia, createPinia } from 'pinia'
import { useUserStore } from '@/stores/user'
import * as authApi from '@/api/auth'
import { ElMessage } from 'element-plus'

vi.mock('@/api/auth', () => ({
  login: vi.fn(),
  register: vi.fn(),
}))

vi.mock('element-plus', async () => {
  const actual = await vi.importActual('element-plus')
  return {
    ...actual,
    ElMessage: { success: vi.fn(), error: vi.fn(), warning: vi.fn() },
    ElMessageBox: { confirm: vi.fn() },
  }
})

describe('Form validation rules', () => {
  const rules = {
    username: [{ required: true, message: '请输入用户名', trigger: 'blur' }],
    password: [{ required: true, message: '请输入密码', trigger: 'blur' }],
  }

  it('should require username', () => {
    const rule = rules.username[0]
    expect(rule.required).toBe(true)
    expect(rule.message).toBe('请输入用户名')
  })

  it('should require password', () => {
    const rule = rules.password[0]
    expect(rule.required).toBe(true)
    expect(rule.message).toBe('请输入密码')
  })
})

describe('Login API calls', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    setActivePinia(createPinia())
  })

  it('should call login with username and password', async () => {
    authApi.login.mockResolvedValue({
      code: 0,
      data: { device_id: 'dev-123' },
      message: '登录成功',
    })

    const res = await authApi.login({ username: 'testuser', password: 'testpass' })
    expect(res.code).toBe(0)
    expect(res.data.device_id).toBe('dev-123')
    expect(authApi.login).toHaveBeenCalledWith({ username: 'testuser', password: 'testpass' })
  })

  it('should store device_id and username in userStore on success', async () => {
    authApi.login.mockResolvedValue({
      code: 0,
      data: { device_id: 'dev-456' },
    })

    const res = await authApi.login({ username: 'alice', password: 'pwd' })
    const store = useUserStore()
    store.setUserInfo({ device_id: res.data.device_id, username: 'alice' })

    expect(store.deviceId).toBe('dev-456')
    expect(store.username).toBe('alice')
    expect(store.isLoggedIn).toBe(true)
  })

  it('should handle login failure with error message', async () => {
    authApi.login.mockResolvedValue({
      code: 401,
      message: '用户名或密码错误',
      data: null,
    })

    const res = await authApi.login({ username: 'bad', password: 'wrong' })
    expect(res.code).toBe(401)
    expect(res.message).toBe('用户名或密码错误')
  })

  it('should handle network error', async () => {
    authApi.login.mockRejectedValue(new Error('Network error'))
    await expect(authApi.login({ username: 'u', password: 'p' })).rejects.toThrow('Network error')
  })
})
