// Shared E2E test helpers — register via API, login via UI
import { test, expect } from '@playwright/test'

// Module-level cache: register once, reuse across all tests in the same worker
let cachedUser = null

export async function loginAsNewUser(page) {
  let username, password

  if (cachedUser) {
    username = cachedUser.username
    password = cachedUser.password
  } else {
    username = `test_user_${Date.now()}`
    password = 'testpass123'

    const regResp = await page.request.post('/api/v1/auth/register', {
      data: {
        username,
        email: `${username}@test.com`,
        password,
      },
    })
    expect(regResp.status()).toBe(200)
    cachedUser = { username, password }
  }

  await page.goto('/login')
  await page.getByPlaceholder('请输入用户名').fill(username)
  await page.getByPlaceholder('请输入密码').fill(password)
  await page.getByRole('button', { name: '登录' }).click()
  await page.waitForURL(/\/dashboard/)

  const devicesResp = await page.request.get('/api/v1/devices')
  const devices = await devicesResp.json()
  const deviceId = devices.data?.[0]?.id

  return { username, deviceId }
}