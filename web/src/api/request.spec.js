import { describe, it, expect, vi } from 'vitest'
import axios from 'axios'

vi.mock('@/router', () => ({ default: { beforeEach: vi.fn() } }))
vi.mock('@/stores/user', () => ({ useUserStore: vi.fn() }))
vi.mock('element-plus', () => ({ ElMessage: { success: vi.fn(), error: vi.fn(), warning: vi.fn() } }))

import request, { downloadBlob } from './request'

describe('request module', () => {
  it('should be an axios instance', () => {
    expect(request instanceof axios.constructor).toBe(true)
    expect(typeof request.get).toBe('function')
    expect(typeof request.post).toBe('function')
  })

  it('should have withCredentials set to true', () => {
    expect(request.defaults.withCredentials).toBe(true)
  })

  it('should have downloadBlob as an async function', () => {
    expect(typeof downloadBlob).toBe('function')
    expect(downloadBlob.constructor.name).toBe('AsyncFunction')
  })
})
