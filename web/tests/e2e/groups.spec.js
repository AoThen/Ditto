import { test, expect } from '@playwright/test'
import { loginAsNewUser } from './helpers'

test.describe('Group Management', () => {
  test('should show empty groups after login', async ({ page }) => {
    await loginAsNewUser(page)

    await page.goto('/dashboard/groups')
    await expect(page.getByRole('heading', { name: '分组管理' })).toBeVisible({ timeout: 10000 })

    await expect(page.getByText('暂无分组')).toBeVisible({ timeout: 10000 })
  })

  test('should create a group via UI', async ({ page }) => {
    await loginAsNewUser(page)
    await page.goto('/dashboard/groups')

    await page.getByRole('button', { name: '创建分组' }).click()

    await page.getByPlaceholder('请输入分组名称').fill('Test Group')
    await page.getByPlaceholder('请输入分组描述').fill('This is a test group')

    await page.getByRole('button', { name: '创建', exact: true }).click()

    await expect(page.getByText('分组已创建')).toBeVisible({ timeout: 10000 })
    await expect(page.locator('.group-tree-card .el-table').getByText('Test Group', { exact: true })).toBeVisible()
  })

  test('should create and delete a group via API', async ({ page, request }) => {
    await loginAsNewUser(page)

    const createResp = await page.request.post('/api/v1/groups', {
      data: {
        name: 'API Test Group',
        description: 'Created via API',
        parent_id: null,
        clip_order: 0
      }
    })
    expect(createResp.status()).toBe(200)
    const createData = await createResp.json()
    expect(createData.code).toBe(0)
    const groupId = createData.data.id

    const listResp = await page.request.get('/api/v1/groups')
    const listData = await listResp.json()
    const found = listData.data?.some(g => g.id === groupId)
    expect(found).toBe(true)

    const deleteResp = await page.request.delete(`/api/v1/groups/${groupId}`)
    expect(deleteResp.status()).toBe(200)

    const afterResp = await page.request.get('/api/v1/groups')
    const afterData = await afterResp.json()
    const stillFound = afterData.data?.some(g => g.id === groupId)
    expect(stillFound).toBe(false)
  })

  test('should create parent and child groups', async ({ page }) => {
    await loginAsNewUser(page)

    const parentResp = await page.request.post('/api/v1/groups', {
      data: {
        name: 'Parent Group',
        description: 'Parent',
        parent_id: null,
        clip_order: 0
      }
    })
    expect(parentResp.status()).toBe(200)
    const parentData = await parentResp.json()
    const parentId = parentData.data.id

    const childResp = await page.request.post('/api/v1/groups', {
      data: {
        name: 'Child Group',
        description: 'Child',
        parent_id: parentId,
        clip_order: 0
      }
    })
    expect(childResp.status()).toBe(200)
    const childData = await childResp.json()
    expect(childData.data.parent_id).toBe(parentId)
  })

  test('should move clips to group via API', async ({ page }) => {
    await loginAsNewUser(page)

    const devicesResp = await page.request.get('/api/v1/devices')
    const devices = await devicesResp.json()
    const deviceId = devices.data?.[0]?.id

    const clipId = `clip-group-${Date.now()}`
    await page.request.post('/api/v1/clips/sync', {
      data: {
        since: '2000-01-01T00:00:00Z',
        device_id: deviceId,
        push_clips: [{
          id: clipId,
          description: 'Clip for Group Test',
          crc: 77777,
          group_id: '',
          short_cut: 0,
          formats: [{ format_type: 13, data: 'Z3JvdXBUZXN0' }]
        }]
      }
    })

    const groupResp = await page.request.post('/api/v1/groups', {
      data: {
        name: 'Move Target',
        description: 'Target for clip move',
        parent_id: null,
        clip_order: 0
      }
    })
    const groupData = await groupResp.json()
    const groupId = groupData.data.id

    const moveResp = await page.request.post(`/api/v1/groups/${groupId}/move-clips`, {
      data: { clip_ids: [clipId] }
    })
    expect(moveResp.status()).toBe(200)

    const clipResp = await page.request.get(`/api/v1/clips/${clipId}`)
    const clipData = await clipResp.json()
    expect(clipData.data.group_id).toBe(groupId)
  })

  test('should navigate to groups from sidebar', async ({ page }) => {
    await loginAsNewUser(page)

    await expect(page.getByRole('menuitem', { name: '分组管理' })).toBeVisible()

    await page.getByRole('menuitem', { name: '分组管理' }).click()
    await expect(page).toHaveURL(/\/dashboard\/groups/)
  })
})