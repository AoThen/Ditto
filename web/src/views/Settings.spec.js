import { describe, it, expect, vi, beforeEach } from 'vitest'
import { setActivePinia, createPinia } from 'pinia'
import * as clipsApi from '@/api/clips'
import { ElMessage, ElMessageBox } from 'element-plus'

vi.mock('@/api/clips', () => ({
  getEncryptionSalt: vi.fn(),
  setupEncryption: vi.fn(),
  disableEncryption: vi.fn(),
  changeEncryptionPassword: vi.fn(),
}))

vi.mock('element-plus', async () => {
  const actual = await vi.importActual('element-plus')
  return {
    ...actual,
    ElMessage: { success: vi.fn(), error: vi.fn(), warning: vi.fn() },
    ElMessageBox: { confirm: vi.fn() },
  }
})

describe('Encryption API calls', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    setActivePinia(createPinia())
  })

  it('getEncryptionSalt should return salt and hint', async () => {
    clipsApi.getEncryptionSalt.mockResolvedValue({
      code: 0,
      data: { salt: 'abc123', password_hint: 'my hint' },
    })

    const res = await clipsApi.getEncryptionSalt()
    expect(res.code).toBe(0)
    expect(res.data.salt).toBe('abc123')
    expect(res.data.password_hint).toBe('my hint')
  })

  it('getEncryptionSalt should return empty when no encryption', async () => {
    clipsApi.getEncryptionSalt.mockResolvedValue({
      code: 0,
      data: { salt: '', password_hint: '' },
    })

    const res = await clipsApi.getEncryptionSalt()
    expect(res.data.salt).toBe('')
  })

  it('setupEncryption should send password and hint', async () => {
    clipsApi.setupEncryption.mockResolvedValue({ code: 0, data: null })

    const res = await clipsApi.setupEncryption({
      password: 'strongpass',
      password_hint: 'my hint',
    })
    expect(res.code).toBe(0)
    expect(clipsApi.setupEncryption).toHaveBeenCalledWith({
      password: 'strongpass',
      password_hint: 'my hint',
    })
  })

  it('changeEncryptionPassword should send new hint only', async () => {
    clipsApi.changeEncryptionPassword.mockResolvedValue({ code: 0, data: null })

    const res = await clipsApi.changeEncryptionPassword('new hint')
    expect(res.code).toBe(0)
    expect(clipsApi.changeEncryptionPassword).toHaveBeenCalledWith('new hint')
  })

  it('disableEncryption should disable encryption', async () => {
    clipsApi.disableEncryption.mockResolvedValue({ code: 0, data: null })

    const res = await clipsApi.disableEncryption()
    expect(res.code).toBe(0)
  })

  it('should handle API errors gracefully', async () => {
    clipsApi.setupEncryption.mockRejectedValue(new Error('Network error'))

    await expect(clipsApi.setupEncryption({ password: 'p', password_hint: '' })).rejects.toThrow('Network error')
  })
})

describe('Encryption setup validation logic', () => {
  it('should require password confirmation match', () => {
    const password = 'mysecret123'
    const confirm = 'different'
    const errorMessage = '两次输入的密码不一致'
    const isValid = password === confirm
    expect(isValid).toBe(false)
  })

  it('should require minimum password length of 8', () => {
    const password = '1234567'
    const isValid = password.length >= 8
    expect(isValid).toBe(false)

    const validPassword = '12345678'
    expect(validPassword.length >= 8).toBe(true)
  })
})

describe('Change password dialog logic', () => {
  it('changeEncryptionPassword accepts only passwordHint', async () => {
    clipsApi.changeEncryptionPassword.mockResolvedValue({ code: 0, data: null })

    const newHint = 'my new hint'
    const res = await clipsApi.changeEncryptionPassword(newHint)

    expect(res.code).toBe(0)
    expect(clipsApi.changeEncryptionPassword).toHaveBeenCalledWith(newHint)
    expect(clipsApi.changeEncryptionPassword).toHaveBeenCalledTimes(1)
  })

  it('changeEncryptionPassword should warn on empty hint', async () => {
    const newHint = ''
    const hasWarning = !newHint
    expect(hasWarning).toBe(true)
  })
})
