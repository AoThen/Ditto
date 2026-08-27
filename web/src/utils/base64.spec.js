import { describe, it, expect } from 'vitest'
import { base64ToUtf8 } from './base64'

describe('base64ToUtf8', () => {
  it('should decode ASCII text', () => {
    // base64("Hello") = "SGVsbG8="
    expect(base64ToUtf8('SGVsbG8=')).toBe('Hello')
  })

  it('should decode UTF-8 Chinese text', () => {
    // base64("你好") = "5L2g5aW9"
    expect(base64ToUtf8('5L2g5aW9')).toBe('你好')
  })

  it('should decode UTF-8 emoji', () => {
    // 😀 = UTF-8 F0 9F 98 80 → base64 "8J+YgA=="
    expect(base64ToUtf8('8J+YgA==')).toBe('😀')
  })

  it('should handle valid base64 with padding', () => {
    // base64("test") = "dGVzdA=="
    expect(base64ToUtf8('dGVzdA==')).toBe('test')
  })

  it('should return null for invalid base64', () => {
    expect(base64ToUtf8('!!!')).toBe(null)
  })

  it('should return null for empty string', () => {
    expect(base64ToUtf8('')).toBe(null)
  })

  it('should decode multi-byte UTF-8 correctly', () => {
    // base64("hello 世界 🌍") should round-trip
    const original = 'hello 世界 🌍'
    const encoded = btoa(unescape(encodeURIComponent(original)))
    expect(base64ToUtf8(encoded)).toBe(original)
  })
})