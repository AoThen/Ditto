import { describe, it, expect } from 'vitest'
import { formatDate, formatShortDate } from '@/composables/useFormatDate'

describe('formatDate', () => {
  it('formats a normal date string', () => {
    expect(formatDate('2025-07-15 10:30:00')).toBe('2025-07-15 10:30')
  })

  it('returns "-" for null input', () => {
    expect(formatDate(null)).toBe('-')
  })

  it('returns "-" for undefined input', () => {
    expect(formatDate(undefined)).toBe('-')
  })

  it('returns "-" for empty string input', () => {
    expect(formatDate('')).toBe('-')
  })

  it('returns "-" for invalid date string', () => {
    expect(formatDate('not-a-date')).toBe('-')
  })

  it('formats midnight correctly', () => {
    expect(formatDate('2025-01-01 00:00:00')).toBe('2025-01-01 00:00')
  })
})

describe('formatShortDate', () => {
  it('formats a normal date string', () => {
    expect(formatShortDate('2025-07-15 10:30:00')).toBe('7/15')
  })

  it('returns "-" for null input', () => {
    expect(formatShortDate(null)).toBe('-')
  })

  it('returns "-" for undefined input', () => {
    expect(formatShortDate(undefined)).toBe('-')
  })

  it('returns "-" for empty string input', () => {
    expect(formatShortDate('')).toBe('-')
  })

  it('returns "-" for invalid date string', () => {
    expect(formatShortDate('not-a-date')).toBe('-')
  })

  it('formats single-digit month and day', () => {
    expect(formatShortDate('2025-03-05 10:30:00')).toBe('3/5')
  })

  it('formats double-digit month and day', () => {
    expect(formatShortDate('2025-11-25 10:30:00')).toBe('11/25')
  })
})