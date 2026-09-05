// Shared E2E test helpers — register via API, login via UI
import { test, expect } from '@playwright/test'

const TEST_USERNAME = process.env.E2E_USERNAME || 'e2e_test_user'
const TEST_PASSWORD = process.env.E2E_PASSWORD || 'e2e_test_pass_123'

let deviceSeq = 0

export async function loginAsNewUser(page) {
  // Idempotent registration — tolerate 403 (closed) and 400 (already exists)
  const regResp = await page.request.post('/api/v1/auth/register', {
    data: {
      username: TEST_USERNAME,
      email: `${TEST_USERNAME}@test.com`,
      password: TEST_PASSWORD,
    },
  })
  expect([200, 400, 403]).toContain(regResp.status())

  // Unique device per test — prevents token-version conflicts across tests
  deviceSeq += 1
  const deviceName = `e2e-${deviceSeq}-${Date.now()}`

  // Intercept SPA login to inject device name (SPA does not send X-Device-Name)
  await page.route('**/api/v1/auth/login', async (route) => {
    const headers = { ...route.request().headers() }
    delete headers['content-length']
    headers['X-Device-Name'] = deviceName
    await route.continue({ headers })
  })

  // UI login so SPA stores session state (route guard) and server sets auth cookie
  await page.goto('/login')
  await page.getByPlaceholder('请输入用户名').fill(TEST_USERNAME)
  await page.getByPlaceholder('请输入密码').fill(TEST_PASSWORD)
  await page.getByRole('button', { name: '登录' }).click()
  await page.waitForURL(/\/dashboard/)

  await page.unroute('**/api/v1/auth/login')

  // Obtain a device token via the API
  const loginResp = await page.request.post('/api/v1/auth/login', {
    data: { username: TEST_USERNAME, password: TEST_PASSWORD },
    headers: { 'X-Device-Name': deviceName },
  })
  expect(loginResp.status()).toBe(200)
  const loginData = await loginResp.json()
  const token = loginData?.data?.device_token
  expect(token).toBeTruthy()

  const deviceId = loginData?.data?.device_id

  // Authenticated API calls must use page.request, which shares the browser
  // context's cookie jar (the HttpOnly auth cookies set by the UI login).
  // The standalone `request` fixture is a separate context with no cookies, and
  // Playwright 1.59 dropped its setExtraHTTPHeaders method.
  return { username: TEST_USERNAME, deviceId }
}
