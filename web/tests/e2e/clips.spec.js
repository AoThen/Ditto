import { test, expect } from '@playwright/test'
import { loginAsNewUser } from './helpers'

test.describe('Clip Management', () => {
  test('should show empty clip list after login', async ({ page, request }) => {
    await loginAsNewUser(page, request)
    await expect(page).toHaveURL(/\/dashboard/)
    await expect(page.getByText('暂无数据', { exact: true })).toBeVisible({ timeout: 10000 })
  })

  test('should create clip via API and verify', async ({ page, request }) => {
    const { deviceId } = await loginAsNewUser(page, request)

    const clipId = `clip-e2e-${Date.now()}`
    const syncResp = await request.post('/api/v1/clips/sync', {
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

    const clipsResp = await request.get('/api/v1/clips?page=1&per_page=10')
    const clipsData = await clipsResp.json()
    expect(clipsData.data.total).toBeGreaterThanOrEqual(1)
    const found = clipsData.data.items?.some(item => item.description === 'E2E API Test Clip')
    expect(found).toBe(true)
  })

  test('should search clips', async ({ page, request }) => {
    const { deviceId } = await loginAsNewUser(page, request)

    await request.post('/api/v1/clips/sync', {
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

    const clipsResp = await request.get('/api/v1/clips?search=UniqueSearchTerm123')
    const clipsData = await clipsResp.json()
    expect(clipsData.data.total).toBeGreaterThanOrEqual(1)

    const emptyResp = await request.get('/api/v1/clips?search=nonexistent_xyz_123')
    const emptyData = await emptyResp.json()
    expect(emptyData.data.total).toBe(0)
  })

  test('should delete a clip via API', async ({ page, request }) => {
    const { deviceId } = await loginAsNewUser(page, request)

    const clipId = `clip-delete-${Date.now()}`
    await request.post('/api/v1/clips/sync', {
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

    const beforeResp = await request.get(`/api/v1/clips/${clipId}`)
    expect(beforeResp.status()).toBe(200)

    const deleteResp = await request.delete(`/api/v1/clips/${clipId}`)
    expect(deleteResp.status()).toBe(200)

    const afterResp = await request.get(`/api/v1/clips/${clipId}`)
    expect(afterResp.status()).toBe(404)
  })
})