import { describe, it, expect, vi, beforeEach } from 'vitest'

const mockImportKey = vi.fn()
const mockDeriveKey = vi.fn()
const mockGenerateKey = vi.fn()
const mockWrapKey = vi.fn()
const mockUnwrapKey = vi.fn()
const mockDigest = vi.fn()
const mockGetRandomValues = vi.fn()

beforeEach(() => {
  vi.clearAllMocks()

  global.crypto = {
    subtle: {
      importKey: mockImportKey,
      deriveKey: mockDeriveKey,
      generateKey: mockGenerateKey,
      wrapKey: mockWrapKey,
      unwrapKey: mockUnwrapKey,
      digest: mockDigest,
    },
    getRandomValues: mockGetRandomValues,
  }

  mockGetRandomValues.mockImplementation((arr) => {
    for (let i = 0; i < arr.length; i++) arr[i] = i
    return arr
  })
})

describe('deriveKEK', () => {
  it('调用 importKey 和 deriveKey', async () => {
    const { deriveKEK } = await import('./crypto.js')
    const fakeKey = { type: 'secret' }
    const fakeKek = { type: 'secret', algorithm: { name: 'AES-GCM' } }
    mockImportKey.mockResolvedValue(fakeKey)
    mockDeriveKey.mockResolvedValue(fakeKek)

    const result = await deriveKEK('mypassword', 'c2FsdA==')

    expect(mockImportKey).toHaveBeenCalledWith(
      'raw', expect.any(ArrayBuffer), 'PBKDF2', false, ['deriveKey']
    )
    expect(mockDeriveKey).toHaveBeenCalledWith(
      { name: 'PBKDF2', salt: expect.any(Uint8Array), iterations: 100000, hash: 'SHA-256' },
      fakeKey,
      { name: 'AES-GCM', length: 256 },
      false, ['wrapKey', 'unwrapKey']
    )
    expect(result).toBe(fakeKek)
  })
})

describe('generateDEK', () => {
  it('调用 crypto.subtle.generateKey', async () => {
    const { generateDEK } = await import('./crypto.js')
    const fakeDek = { type: 'secret' }
    mockGenerateKey.mockResolvedValue(fakeDek)

    const result = await generateDEK()

    expect(mockGenerateKey).toHaveBeenCalledWith(
      { name: 'AES-GCM', length: 256 },
      true, ['encrypt', 'decrypt']
    )
    expect(result).toBe(fakeDek)
  })
})

describe('wrapDEK', () => {
  it('调用 wrapKey 并返回 base64 字符串', async () => {
    const { wrapDEK } = await import('./crypto.js')
    const fakeKek = { type: 'secret' }
    const fakeDek = { type: 'secret' }
    const wrappedBytes = new Uint8Array([10, 20, 30, 40])
    mockWrapKey.mockResolvedValue(wrappedBytes.buffer)

    const result = await wrapDEK(fakeKek, fakeDek)

    const iv = new Uint8Array(12)
    for (let i = 0; i < 12; i++) iv[i] = i
    expect(mockGetRandomValues).toHaveBeenCalled()
    expect(mockWrapKey).toHaveBeenCalledWith(
      'raw', fakeDek, fakeKek, { name: 'AES-GCM', iv: expect.any(Uint8Array) }
    )
    expect(typeof result).toBe('string')
    expect(result.length).toBeGreaterThan(0)
  })
})

describe('unwrapDEK', () => {
  it('调用 unwrapKey 并返回 DEK', async () => {
    const { unwrapDEK } = await import('./crypto.js')
    const fakeKek = { type: 'secret' }
    const fakeDek = { type: 'secret' }
    mockUnwrapKey.mockResolvedValue(fakeDek)

    const iv = new Uint8Array(12)
    for (let i = 0; i < 12; i++) iv[i] = i
    const wrapped = new Uint8Array([10, 20, 30, 40])
    const combined = new Uint8Array(iv.length + wrapped.length)
    combined.set(iv)
    combined.set(wrapped, iv.length)
    const combinedB64 = btoa(String.fromCharCode(...combined))

    const result = await unwrapDEK(fakeKek, combinedB64)

    expect(mockUnwrapKey).toHaveBeenCalledWith(
      'raw', wrapped, fakeKek, { name: 'AES-GCM', iv },
      { name: 'AES-GCM', length: 256 },
      true, ['encrypt', 'decrypt']
    )
    expect(result).toBe(fakeDek)
  })
})

describe('computeVerificationHash', () => {
  it('调用 digest 并返回 base64 字符串', async () => {
    const { computeVerificationHash } = await import('./crypto.js')
    const hashBytes = new Uint8Array([1, 2, 3, 4, 5, 6, 7, 8])
    mockDigest.mockResolvedValue(hashBytes.buffer)

    const result = await computeVerificationHash('mypassword', 'c2FsdA==')

    expect(mockDigest).toHaveBeenCalledWith('SHA-256', expect.any(ArrayBuffer))
    expect(typeof result).toBe('string')
    expect(result).toBe(btoa(String.fromCharCode(...hashBytes)))
  })
})

describe('wrapDEK + unwrapDEK', () => {
  it('mock wrapKey 返回已知值，验证 unwrapDEK 正确解析', async () => {
    const { wrapDEK, unwrapDEK } = await import('./crypto.js')
    const fakeKek = { type: 'secret' }
    const fakeDek = { type: 'secret' }
    const wrappedBytes = new Uint8Array([100, 110, 120])
    mockWrapKey.mockResolvedValue(wrappedBytes.buffer)
    mockUnwrapKey.mockResolvedValue(fakeDek)

    const wrappedB64 = await wrapDEK(fakeKek, fakeDek)
    const result = await unwrapDEK(fakeKek, wrappedB64)

    const iv = new Uint8Array(12)
    for (let i = 0; i < 12; i++) iv[i] = i

    expect(mockWrapKey).toHaveBeenCalledWith(
      'raw', fakeDek, fakeKek, { name: 'AES-GCM', iv: expect.any(Uint8Array) }
    )
    expect(mockUnwrapKey).toHaveBeenCalledWith(
      'raw', wrappedBytes, fakeKek, { name: 'AES-GCM', iv },
      { name: 'AES-GCM', length: 256 },
      true, ['encrypt', 'decrypt']
    )
    expect(result).toBe(fakeDek)
  })
})
