// Shared E2E test helpers — register via API, login via UI
import { test, expect } from '@playwright/test'

export async function loginAsNewUser(page) {
  const timestamp = Date.now()
  const username = `test_user_${timestamp}`

  const regResp = await page.request.post('http://localhost:8080/api/v1/auth/register', {
    data: {
      username,
      email: `${username}@test.com`,
      password: 'testpass123',
    },
  })
  expect(regResp.status()).toBe(200)

  await page.goto('/login')
  await page.getByPlaceholder('请输入用户名').fill(username)
  await page.getByPlaceholder('请输入密码').fill('testpass123')
  await page.getByRole('button', { name: '登录' }).click()
  await page.waitForURL(/\/dashboard/)

  const devicesResp = await page.request.get('http://localhost:8080/api/v1/devices')
  const devices = await devicesResp.json()
  const deviceId = devices.data?.[0]?.id

  return { username, deviceId }
}