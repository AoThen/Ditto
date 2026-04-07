// Test clip management: list, search, delete
import { test, expect } from '@playwright/test'

// Helper: login and get to dashboard
async function loginAsNewUser(page) {
  const timestamp = Date.now()
  const username = `clip_user_${timestamp}`
  
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
  
  // Get actual device_id from devices API
  const devicesResp = await page.request.get('http://localhost:8080/api/v1/devices', {
    headers: { 'Authorization': `Bearer ${token}` }
  })
  const devices = await devicesResp.json()
  const deviceId = devices.data?.[0]?.id
  
  return { username, token, deviceId }
}

test.describe('Clip Management', () => {
  test('should show empty clip list after login', async ({ page }) => {
    await loginAsNewUser(page)
    await expect(page).toHaveURL(/\/dashboard\/clips/)
    await expect(page.getByText(/暂无剪贴板|暂无数据|暂无/)).toBeVisible({ timeout: 10000 })
  })

  test('should create clip via API and verify', async ({ page }) => {
    const { token, deviceId } = await loginAsNewUser(page)
    
    // Create clip via API
    const clipId = `clip-e2e-${Date.now()}`
    const syncResp = await page.request.post('http://localhost:8080/api/v1/clips/sync', {
      headers: {
        'Authorization': `Bearer ${token}`,
        'Content-Type': 'application/json'
      },
      data: {
        since: '2000-01-01T00:00:00Z',
        device_id: deviceId,
        push_clips: [{
          id: clipId,
          description: 'E2E API Test Clip',
          crc: 11111111,
          group_id: '',
          short_cut: 0,
          formats: [{ format_type: 13, data: 'VGVzdA==' }]
        }]
      }
    })
    expect(syncResp.status()).toBe(200)

    // Verify clip exists via API
    const clipsResp = await page.request.get('http://localhost:8080/api/v1/clips?page=1&per_page=10', {
      headers: { 'Authorization': `Bearer ${token}` }
    })
    const clipsData = await clipsResp.json()
    expect(clipsData.data.total).toBeGreaterThanOrEqual(1)
    const found = clipsData.data.items?.some(item => item.description === 'E2E API Test Clip')
    expect(found).toBe(true)
  })

  test('should search clips', async ({ page }) => {
    const { token, deviceId } = await loginAsNewUser(page)
    
    // Create a clip to search for
    await page.request.post('http://localhost:8080/api/v1/clips/sync', {
      headers: {
        'Authorization': `Bearer ${token}`,
        'Content-Type': 'application/json'
      },
      data: {
        since: '2000-01-01T00:00:00Z',
        device_id: deviceId,
        push_clips: [{
          id: `clip-search-${Date.now()}`,
          description: 'UniqueSearchTerm123',
          crc: 99999,
          group_id: '',
          short_cut: 0,
          formats: [{ format_type: 13, data: 'dGVzdA==' }]
        }]
      }
    })

    // Search for the clip
    const clipsResp = await page.request.get('http://localhost:8080/api/v1/clips?search=UniqueSearchTerm123', {
      headers: { 'Authorization': `Bearer ${token}` }
    })
    const clipsData = await clipsResp.json()
    expect(clipsData.data.total).toBeGreaterThanOrEqual(1)

    // Search for non-existent term
    const emptyResp = await page.request.get('http://localhost:8080/api/v1/clips?search=nonexistent_xyz_123', {
      headers: { 'Authorization': `Bearer ${token}` }
    })
    const emptyData = await emptyResp.json()
    expect(emptyData.data.total).toBe(0)
  })

  test('should delete a clip via API', async ({ page }) => {
    const { token, deviceId } = await loginAsNewUser(page)
    
    // Create a clip
    const clipId = `clip-delete-${Date.now()}`
    await page.request.post('http://localhost:8080/api/v1/clips/sync', {
      headers: {
        'Authorization': `Bearer ${token}`,
        'Content-Type': 'application/json'
      },
      data: {
        since: '2000-01-01T00:00:00Z',
        device_id: deviceId,
        push_clips: [{
          id: clipId,
          description: 'Clip to Delete',
          crc: 22222222,
          group_id: '',
          short_cut: 0,
          formats: [{ format_type: 13, data: 'ZGVsZXRl' }]
        }]
      }
    })

    // Verify clip exists
    const beforeResp = await page.request.get(`http://localhost:8080/api/v1/clips/${clipId}`, {
      headers: { 'Authorization': `Bearer ${token}` }
    })
    expect(beforeResp.status()).toBe(200)

    // Delete the clip
    const deleteResp = await page.request.delete(`http://localhost:8080/api/v1/clips/${clipId}`, {
      headers: { 'Authorization': `Bearer ${token}` }
    })
    expect(deleteResp.status()).toBe(200)

    // Verify clip is gone
    const afterResp = await page.request.get(`http://localhost:8080/api/v1/clips/${clipId}`, {
      headers: { 'Authorization': `Bearer ${token}` }
    })
    expect(afterResp.status()).toBe(404)
  })
})
