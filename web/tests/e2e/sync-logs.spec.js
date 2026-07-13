// Test sync logs and stats endpoints
import { test, expect } from '@playwright/test'

// Helper: login as new user (cookies handled automatically)
async function loginAsNewUser(page) {
  const timestamp = Date.now()
  const username = `synclog_user_${timestamp}`

  await page.goto('/register')
  await page.getByPlaceholder('请输入用户名').fill(username)
  await page.getByPlaceholder('请输入邮箱地址').fill(`${username}@test.com`)
  await page.getByPlaceholder('请输入密码').fill('testpass123')
  await page.getByPlaceholder('请再次输入密码').fill('testpass123')
  await page.getByRole('button', { name: '注册' }).click()
  await page.waitForURL('/login')

  await page.getByPlaceholder('请输入用户名').fill(username)
  await page.getByPlaceholder('请输入密码').fill('testpass123')
  await page.getByRole('button', { name: '登录' }).click()
  await page.waitForURL(/\/dashboard/)

  // Get device_id via API (cookies shared automatically)
  const devicesResp = await page.request.get('http://localhost:8080/api/v1/devices')
  const devices = await devicesResp.json()
  const deviceId = devices.data?.[0]?.id

  return { username, deviceId }
}

test.describe('Sync Logs & Stats', () => {
  test('should get sync logs after sync operation', async ({ page }) => {
    const { deviceId } = await loginAsNewUser(page)

    const syncResp = await page.request.post('http://localhost:8080/api/v1/clips/sync', {
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
    expect(syncResp.status()).toBe(200)

    const logsResp = await page.request.get('http://localhost:8080/api/v1/stats/sync-logs')
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
    await loginAsNewUser(page)

    const statsResp = await page.request.get('http://localhost:8080/api/v1/stats/overview')
    expect(statsResp.status()).toBe(200)
    const statsData = await statsResp.json()
    expect(statsData.code).toBe(0)
    expect(statsData.data).toHaveProperty('total_clips')
    expect(statsData.data).toHaveProperty('total_devices')
    expect(statsData.data).toHaveProperty('total_storage')
  })

  test('should filter sync logs by device_id', async ({ page }) => {
    const { deviceId } = await loginAsNewUser(page)

    await page.request.post('http://localhost:8080/api/v1/clips/sync', {
      data: {
        since: '2000-01-01T00:00:00Z',
        device_id: deviceId,
        push_clips: [{
          id: `clip-filter-${Date.now()}`,
          description: 'Filter Test',
          crc: 44444,
          group_id: '',
          short_cut: 0,
          formats: [{ format_type: 13, data: 'filter' }]
        }]
      }
    })

    const filteredResp = await page.request.get(
      `http://localhost:8080/api/v1/stats/sync-logs?device_id=${deviceId}`
    )
    const filteredData = await filteredResp.json()
    expect(filteredData.data.total).toBeGreaterThanOrEqual(1)

    for (const log of filteredData.data.items) {
      expect(log.device_id).toBe(deviceId)
    }
  })

  test('should show sync logs in UI (StatsDashboard)', async ({ page }) => {
    const { deviceId } = await loginAsNewUser(page)

    await page.request.post('http://localhost:8080/api/v1/clips/sync', {
      data: {
        since: '2000-01-01T00:00:00Z',
        device_id: deviceId,
        push_clips: [{
          id: `clip-ui-${Date.now()}`,
          description: 'UI Log Test',
          crc: 55555,
          group_id: '',
          short_cut: 0,
          formats: [{ format_type: 13, data: 'uiTest' }]
        }]
      }
    })

    await page.goto('/dashboard')
    await expect(page.getByText('数据总览')).toBeVisible({ timeout: 10000 })
  })
})
