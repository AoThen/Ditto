// Test sync logs and stats endpoints
import { test, expect } from '@playwright/test'

// Helper: login and get token + device_id
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

  const token = await page.evaluate(() => localStorage.getItem('token'))

  // Get device_id
  const devicesResp = await page.request.get('http://localhost:8080/api/v1/devices', {
    headers: { 'Authorization': `Bearer ${token}` }
  })
  const devices = await devicesResp.json()
  const deviceId = devices.data?.[0]?.id

  return { username, token, deviceId }
}

test.describe('Sync Logs & Stats', () => {
  test('should get sync logs after sync operation', async ({ page }) => {
    const { token, deviceId } = await loginAsNewUser(page)

    // Perform a sync operation
    const syncResp = await page.request.post('http://localhost:8080/api/v1/clips/sync', {
      headers: {
        'Authorization': `Bearer ${token}`,
        'Content-Type': 'application/json'
      },
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

    // Get sync logs
    const logsResp = await page.request.get('http://localhost:8080/api/v1/stats/sync-logs', {
      headers: { 'Authorization': `Bearer ${token}` }
    })
    expect(logsResp.status()).toBe(200)
    const logsData = await logsResp.json()
    expect(logsData.code).toBe(0)
    expect(logsData.data.total).toBeGreaterThanOrEqual(1)
    expect(logsData.data.items?.length).toBeGreaterThanOrEqual(1)

    // Verify log content
    const logEntry = logsData.data.items[0]
    expect(logEntry.action).toBe('push')
    expect(logEntry.status).toBe('success')
  })

  test('should get stats overview', async ({ page }) => {
    const { token } = await loginAsNewUser(page)

    const statsResp = await page.request.get('http://localhost:8080/api/v1/stats/overview', {
      headers: { 'Authorization': `Bearer ${token}` }
    })
    expect(statsResp.status()).toBe(200)
    const statsData = await statsResp.json()
    expect(statsData.code).toBe(0)
    expect(statsData.data).toHaveProperty('total_clips')
    expect(statsData.data).toHaveProperty('total_devices')
    expect(statsData.data).toHaveProperty('total_storage')
  })

  test('should filter sync logs by device_id', async ({ page }) => {
    const { token, deviceId } = await loginAsNewUser(page)

    // Perform a sync
    await page.request.post('http://localhost:8080/api/v1/clips/sync', {
      headers: {
        'Authorization': `Bearer ${token}`,
        'Content-Type': 'application/json'
      },
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

    // Filter logs by device_id
    const filteredResp = await page.request.get(
      `http://localhost:8080/api/v1/stats/sync-logs?device_id=${deviceId}`,
      { headers: { 'Authorization': `Bearer ${token}` } }
    )
    const filteredData = await filteredResp.json()
    expect(filteredData.data.total).toBeGreaterThanOrEqual(1)

    // All entries should match device_id
    for (const log of filteredData.data.items) {
      expect(log.device_id).toBe(deviceId)
    }
  })

  test('should show sync logs in UI (StatsDashboard)', async ({ page }) => {
    const { token, deviceId } = await loginAsNewUser(page)

    // Create a clip to ensure there's data
    await page.request.post('http://localhost:8080/api/v1/clips/sync', {
      headers: {
        'Authorization': `Bearer ${token}`,
        'Content-Type': 'application/json'
      },
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

    // Navigate to dashboard (stats page)
    await page.goto('/dashboard')
    await expect(page.getByText('剪贴板总览')).toBeVisible({ timeout: 10000 })
  })
})
