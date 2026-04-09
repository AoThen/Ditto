// Test group management: create, edit, delete, move clips
import { test, expect } from '@playwright/test'

// Helper: register and login as new user
async function loginAsNewUser(page) {
  const timestamp = Date.now()
  const username = `group_user_${timestamp}`

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

  return { username }
}

test.describe('Group Management', () => {
  test('should show empty groups after login', async ({ page }) => {
    await loginAsNewUser(page)

    await page.goto('/dashboard/groups')
    await expect(page.getByText('分组管理')).toBeVisible({ timeout: 10000 })

    await expect(page.getByText('暂无分组')).toBeVisible({ timeout: 10000 })
  })

  test('should create a group via UI', async ({ page }) => {
    await loginAsNewUser(page)
    await page.goto('/dashboard/groups')

    await page.getByRole('button', { name: '创建分组' }).click()

    await page.getByPlaceholder('请输入分组名称').fill('Test Group')
    await page.getByPlaceholder('请输入分组描述').fill('This is a test group')

    await page.getByRole('button', { name: '创建' }).click()

    await expect(page.getByText('分组已创建')).toBeVisible({ timeout: 10000 })
    await expect(page.getByText('Test Group')).toBeVisible()
  })

  test('should create and delete a group via API', async ({ page, request }) => {
    // Login first to establish cookies in browser context
    await loginAsNewUser(page)

    // Use page.request (shares browser cookies) to call API
    const createResp = await page.request.post('http://localhost:8080/api/v1/groups', {
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

    // Verify group exists
    const listResp = await page.request.get('http://localhost:8080/api/v1/groups')
    const listData = await listResp.json()
    const found = listData.data?.some(g => g.id === groupId)
    expect(found).toBe(true)

    // Delete group
    const deleteResp = await page.request.delete(`http://localhost:8080/api/v1/groups/${groupId}`)
    expect(deleteResp.status()).toBe(200)

    // Verify group is gone
    const afterResp = await page.request.get('http://localhost:8080/api/v1/groups')
    const afterData = await afterResp.json()
    const stillFound = afterData.data?.some(g => g.id === groupId)
    expect(stillFound).toBe(false)
  })

  test('should create parent and child groups', async ({ page }) => {
    await loginAsNewUser(page)

    // Create parent group
    const parentResp = await page.request.post('http://localhost:8080/api/v1/groups', {
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

    // Create child group
    const childResp = await page.request.post('http://localhost:8080/api/v1/groups', {
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

    // Get device_id
    const devicesResp = await page.request.get('http://localhost:8080/api/v1/devices')
    const devices = await devicesResp.json()
    const deviceId = devices.data?.[0]?.id

    // Create a clip
    const clipId = `clip-group-${Date.now()}`
    await page.request.post('http://localhost:8080/api/v1/clips/sync', {
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

    // Create group
    const groupResp = await page.request.post('http://localhost:8080/api/v1/groups', {
      data: {
        name: 'Move Target',
        description: 'Target for clip move',
        parent_id: null,
        clip_order: 0
      }
    })
    const groupData = await groupResp.json()
    const groupId = groupData.data.id

    // Move clip to group
    const moveResp = await page.request.post(`http://localhost:8080/api/v1/groups/${groupId}/move-clips`, {
      data: { clip_ids: [clipId] }
    })
    expect(moveResp.status()).toBe(200)

    // Verify clip has group_id
    const clipResp = await page.request.get(`http://localhost:8080/api/v1/clips/${clipId}`)
    const clipData = await clipResp.json()
    expect(clipData.data.group_id).toBe(groupId)
  })

  test('should navigate to groups from sidebar', async ({ page }) => {
    await loginAsNewUser(page)

    await expect(page.getByText('分组管理')).toBeVisible()

    await page.getByText('分组管理').click()
    await expect(page).toHaveURL(/\/dashboard\/groups/)
  })
})
