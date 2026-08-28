import { test, expect } from '@playwright/test'
import { loginAsNewUser } from './helpers'

test.describe('Sync Logs & Stats', () => {
  test('should get sync logs after sync operation', async ({ page }) => {
    const { deviceId, request } = await loginAsNewUser(page)

    await request.post('/api/v1/clips/sync', {
      data: {
        since: '2000-01-01T00:00:00Z',
        device_id: deviceId,
        push_clips: [{
          id: `clip-synclog-${Date.now()}`,
          description: 'Sync Log Test Clip',
          crc: 33333333,
          group_id: '',
          short_cut: 0,
          formats: [{ format_type: 13, data: 'c3luY0xvZw==' }]
        }]
      }
    })

    const logsResp = await request.get('/api/v1/stats/sync-logs')
    expect(logsResp.status()).toBe(200)
    const logsData = await logsResp.json()
    expect(logsData.code).toBe(0)
    expect(logsData.data.total).toBeGreaterThanOrEqual(1)
    expect(logsData.data.items?.length).toBeGreaterThanOrEqual(1)

    const logEntry = logsData.data.items[0]
    expect(logEntry.action).toBe('push')
    expect(logEntry.status).toBe('success')
  })

  test('should get stats overview', async ({ page }) => {
    const { request } = await loginAsNewUser(page)

    const statsResp = await request.get('/api/v1/stats/overview')
    expect(statsResp.status()).toBe(200)
    const statsData = await statsResp.json()
    expect(statsData.code).toBe(0)
    expect(statsData.data).toHaveProperty('total_clips')
    expect(statsData.data).toHaveProperty('total_devices')
    expect(statsData.data).toHaveProperty('total_storage')
  })

  test('should filter sync logs by device_id', async ({ page }) => {
    const { deviceId, request } = await loginAsNewUser(page)

    await request.post('/api/v1/clips/sync', {
      data: {
        since: '2000-01-01T00:00:00Z',
        device_id: deviceId,
        push_clips: [{
          id: `clip-filter-${Date.now()}`,
          description: 'Filter Test',
          crc: 44444,
          group_id: '',
          short_cut: 0,
          formats: [{ format_type: 13, data: 'ZmlsdGVy' }]
        }]
      }
    })

    const filteredResp = await request.get(
      `/api/v1/stats/sync-logs?device_id=${deviceId}`
    )
    const filteredData = await filteredResp.json()
    expect(filteredData.data.total).toBeGreaterThanOrEqual(1)

    for (const log of filteredData.data.items) {
      expect(log.device_id).toBe(deviceId)
    }
  })

  test('should show sync logs in UI (StatsDashboard)', async ({ page }) => {
    const { deviceId, request } = await loginAsNewUser(page)

    await request.post('/api/v1/clips/sync', {
      data: {
        since: '2000-01-01T00:00:00Z',
        device_id: deviceId,
        push_clips: [{
          id: `clip-ui-${Date.now()}`,
          description: 'UI Log Test',
          crc: 55555,
          group_id: '',
          short_cut: 0,
          formats: [{ format_type: 13, data: 'dWlUZXN0' }]
        }]
      }
    })

    await page.goto('/dashboard')
    await expect(page.getByText('数据总览')).toBeVisible({ timeout: 10000 })
  })
})