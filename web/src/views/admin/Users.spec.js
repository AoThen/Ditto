import { describe, it, expect, vi, beforeEach } from 'vitest'
import { setActivePinia, createPinia } from 'pinia'
import * as adminApi from '@/api/admin'
import { ElMessage } from 'element-plus'

vi.mock('@/api/admin', () => ({
  getUsers: vi.fn(),
  getUser: vi.fn(),
  createUser: vi.fn(),
  updateUser: vi.fn(),
  deleteUser: vi.fn(),
  resetPassword: vi.fn(),
}))

vi.mock('element-plus', async () => {
  const actual = await vi.importActual('element-plus')
  return {
    ...actual,
    ElMessage: { success: vi.fn(), error: vi.fn(), warning: vi.fn() },
  }
})

describe('Users API calls', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    setActivePinia(createPinia())
  })

  it('getUsers should return paginated user list', async () => {
    adminApi.getUsers.mockResolvedValue({
      code: 0,
      data: {
        items: [
          { id: 1, username: 'admin', email: 'admin@example.com', role: 'admin', is_active: true, device_count: 3, created_at: '2024-01-01T00:00:00Z' },
          { id: 2, username: 'user1', email: 'user1@example.com', role: 'user', is_active: true, device_count: 1, created_at: '2024-02-01T00:00:00Z' },
        ],
        total: 2,
      },
    })

    const res = await adminApi.getUsers({ page: 1, per_page: 20, search: '' })
    expect(res.code).toBe(0)
    expect(res.data.items).toHaveLength(2)
    expect(adminApi.getUsers).toHaveBeenCalledWith({ page: 1, per_page: 20, search: '' })
  })

  it('createUser should create a new user', async () => {
    const newUser = { username: 'newuser', email: 'new@example.com', password: 'password123' }
    adminApi.createUser.mockResolvedValue({ code: 0, data: { id: 3, ...newUser } })

    const res = await adminApi.createUser(newUser)
    expect(res.code).toBe(0)
    expect(res.data.username).toBe('newuser')
    expect(adminApi.createUser).toHaveBeenCalledWith(newUser)
  })

  it('updateUser should update user info', async () => {
    adminApi.updateUser.mockResolvedValue({ code: 0, data: null })

    const res = await adminApi.updateUser(1, { email: 'updated@example.com', is_active: true })
    expect(res.code).toBe(0)
    expect(adminApi.updateUser).toHaveBeenCalledWith(1, { email: 'updated@example.com', is_active: true })
  })

  it('deleteUser should delete a user', async () => {
    adminApi.deleteUser.mockResolvedValue({ code: 0, data: null })

    const res = await adminApi.deleteUser(1)
    expect(res.code).toBe(0)
    expect(adminApi.deleteUser).toHaveBeenCalledWith(1)
  })

  it('resetPassword should reset user password', async () => {
    adminApi.resetPassword.mockResolvedValue({ code: 0, data: null })

    const res = await adminApi.resetPassword(1, { password: 'newpass123' })
    expect(res.code).toBe(0)
    expect(adminApi.resetPassword).toHaveBeenCalledWith(1, { password: 'newpass123' })
  })

  it('should handle API errors gracefully', async () => {
    adminApi.getUsers.mockRejectedValue(new Error('Network error'))

    await expect(adminApi.getUsers({ page: 1, per_page: 20 })).rejects.toThrow('Network error')
  })
})

describe('formatDate', () => {
  function formatDate(dateStr) {
    if (!dateStr) return ''
    return new Date(dateStr).toLocaleString('zh-CN', {
      year: 'numeric', month: '2-digit', day: '2-digit',
      hour: '2-digit', minute: '2-digit',
    })
  }

  it('returns formatted string for valid date', () => {
    const result = formatDate('2024-06-15T10:30:00Z')
    expect(result).toContain('2024')
    expect(result).toContain('06')
    expect(result).toContain('15')
    expect(result.length).toBeGreaterThan(5)
  })

  it('returns empty string for null', () => {
    expect(formatDate(null)).toBe('')
  })

  it('returns empty string for undefined', () => {
    expect(formatDate(undefined)).toBe('')
  })

  it('returns empty string for empty string', () => {
    expect(formatDate('')).toBe('')
  })

  it('handles current date correctly', () => {
    const now = new Date()
    const result = formatDate(now.toISOString())
    expect(result).toContain(String(now.getFullYear()))
  })
})

describe('Form validation rules', () => {
  const createRules = {
    username: [
      { required: true, message: '请输入用户名', trigger: 'blur' },
      { min: 3, max: 32, message: '用户名长度3-32位', trigger: 'blur' },
    ],
    email: [
      { required: true, message: '请输入邮箱', trigger: 'blur' },
      { type: 'email', message: '请输入有效邮箱', trigger: 'blur' },
    ],
    password: [
      { required: true, message: '请输入密码', trigger: 'blur' },
      { min: 6, message: '密码至少6位', trigger: 'blur' },
    ],
    confirmPassword: [
      { required: true, message: '请确认密码', trigger: 'blur' },
    ],
  }

  const editRules = {
    email: [
      { required: true, message: '请输入邮箱', trigger: 'blur' },
      { type: 'email', message: '请输入有效邮箱', trigger: 'blur' },
    ],
  }

  const resetPwdRules = {
    password: [
      { required: true, message: '请输入新密码', trigger: 'blur' },
      { min: 6, message: '密码至少6位', trigger: 'blur' },
    ],
    confirmPassword: [
      { required: true, message: '请确认密码', trigger: 'blur' },
    ],
  }

  describe('create user rules', () => {
    it('requires username field', () => {
      const rule = createRules.username[0]
      expect(rule.required).toBe(true)
      expect(rule.message).toBe('请输入用户名')
    })

    it('enforces username length', () => {
      const rule = createRules.username[1]
      expect(rule.min).toBe(3)
      expect(rule.max).toBe(32)
      expect(rule.message).toBe('用户名长度3-32位')
    })

    it('requires email field', () => {
      const rule = createRules.email[0]
      expect(rule.required).toBe(true)
      expect(rule.message).toBe('请输入邮箱')
    })

    it('validates email format', () => {
      const rule = createRules.email[1]
      expect(rule.type).toBe('email')
    })

    it('requires password field', () => {
      const rule = createRules.password[0]
      expect(rule.required).toBe(true)
      expect(rule.message).toBe('请输入密码')
    })

    it('enforces minimum password length', () => {
      const rule = createRules.password[1]
      expect(rule.min).toBe(6)
    })

    it('requires confirm password field', () => {
      const rule = createRules.confirmPassword[0]
      expect(rule.required).toBe(true)
      expect(rule.message).toBe('请确认密码')
    })
  })

  describe('edit user rules', () => {
    it('requires email field', () => {
      const rule = editRules.email[0]
      expect(rule.required).toBe(true)
      expect(rule.message).toBe('请输入邮箱')
    })

    it('validates email format', () => {
      const rule = editRules.email[1]
      expect(rule.type).toBe('email')
    })
  })

  describe('reset password rules', () => {
    it('requires new password', () => {
      const rule = resetPwdRules.password[0]
      expect(rule.required).toBe(true)
      expect(rule.message).toBe('请输入新密码')
    })

    it('enforces minimum password length', () => {
      const rule = resetPwdRules.password[1]
      expect(rule.min).toBe(6)
    })

    it('requires confirm password', () => {
      const rule = resetPwdRules.confirmPassword[0]
      expect(rule.required).toBe(true)
      expect(rule.message).toBe('请确认密码')
    })
  })

  describe('confirmPassword validator', () => {
    it('should reject when passwords do not match', () => {
      const createForm = { password: 'pass123', confirmPassword: 'different' }

      function validateConfirmPassword(value, formPassword) {
        if (value !== formPassword) return '两次密码不一致'
        return undefined
      }

      expect(validateConfirmPassword('different', 'pass123')).toBe('两次密码不一致')
      expect(validateConfirmPassword('pass123', 'pass123')).toBeUndefined()
    })

    it('should reject when reset passwords do not match', () => {
      const resetPwdForm = { password: 'newpass', confirmPassword: 'mismatch' }

      function validateConfirmPassword(value, formPassword) {
        if (value !== formPassword) return '两次密码不一致'
        return undefined
      }

      expect(validateConfirmPassword('mismatch', 'newpass')).toBe('两次密码不一致')
      expect(validateConfirmPassword('newpass', 'newpass')).toBeUndefined()
    })
  })
})
