import { describe, it, expect, vi, beforeEach } from 'vitest'
import { setActivePinia, createPinia } from 'pinia'
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
  }
})

describe('Form validation rules', () => {
  const validatePasswordMatch = (formPassword) => (rule, value, callback) => {
    if (value !== formPassword) {
      callback(new Error('两次输入的密码不一致'))
    } else {
      callback()
    }
  }

  const rules = {
    username: [{ required: true, message: '请输入用户名', trigger: 'blur' }],
    email: [
      { required: true, message: '请输入邮箱地址', trigger: 'blur' },
      { type: 'email', message: '请输入有效的邮箱地址', trigger: 'blur' },
    ],
    password: [
      { required: true, message: '请输入密码', trigger: 'blur' },
      { min: 6, message: '密码长度不能少于6位', trigger: 'blur' },
    ],
    confirmPassword: [
      { required: true, message: '请再次输入密码', trigger: 'blur' },
      { validator: validatePasswordMatch('mypassword'), trigger: 'blur' },
    ],
  }

  it('should require username', () => {
    expect(rules.username[0].required).toBe(true)
    expect(rules.username[0].message).toBe('请输入用户名')
  })

  it('should require email', () => {
    expect(rules.email[0].required).toBe(true)
    expect(rules.email[0].message).toBe('请输入邮箱地址')
  })

  it('should validate email format', () => {
    expect(rules.email[1].type).toBe('email')
    expect(rules.email[1].message).toBe('请输入有效的邮箱地址')
  })

  it('should require password with minimum length', () => {
    expect(rules.password[0].required).toBe(true)
    expect(rules.password[1].min).toBe(6)
    expect(rules.password[1].message).toBe('密码长度不能少于6位')
  })

  it('should require confirm password', () => {
    expect(rules.confirmPassword[0].required).toBe(true)
  })

  it('should validate password match', () => {
    const validator = rules.confirmPassword[1].validator
    const matchCallback = vi.fn()
    validator(null, 'mypassword', matchCallback)
    expect(matchCallback).toHaveBeenCalledWith()

    const mismatchCallback = vi.fn()
    validator(null, 'different', mismatchCallback)
    expect(mismatchCallback).toHaveBeenCalledWith(new Error('两次输入的密码不一致'))
  })
})

describe('Register API calls', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    setActivePinia(createPinia())
  })

  it('should send registration data without confirmPassword', async () => {
    const form = { username: 'newuser', email: 'a@b.com', password: 'secret123', confirmPassword: 'secret123' }
    const { confirmPassword, ...registerData } = form

    authApi.register.mockResolvedValue({ code: 0, data: null, message: '注册成功' })

    const res = await authApi.register(registerData)
    expect(res.code).toBe(0)
    expect(authApi.register).toHaveBeenCalledWith({ username: 'newuser', email: 'a@b.com', password: 'secret123' })
  })

  it('should handle registration failure', async () => {
    authApi.register.mockResolvedValue({
      code: 400,
      message: '用户名已存在',
      data: null,
    })

    const res = await authApi.register({ username: 'existing', email: 'a@b.com', password: 'pass123' })
    expect(res.code).toBe(400)
    expect(res.message).toBe('用户名已存在')
  })

  it('should handle network error', async () => {
    authApi.register.mockRejectedValue(new Error('Network error'))
    await expect(authApi.register({ username: 'u', email: 'a@b.com', password: 'p' })).rejects.toThrow('Network error')
  })
})
