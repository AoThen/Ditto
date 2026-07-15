// Shared E2E test helpers — register via API, login via UI
import { test, expect } from '@playwright/test'

// Fixed credentials so authentication is idempotent across spec files and test retries.
// The server only allows the first user to register (RegisterAllowed), so generating a
// brand-new random user per call would fail with 403 on every retry / second registration.
const TEST_USERNAME = process.env.E2E_USERNAME || 'e2e_test_user'
const TEST_PASSWORD = process.env.E2E_PASSWORD || 'e2e_test_pass_123'

// Ensure the test account exists.
// Registration is only allowed when no users exist (the first user becomes admin), so we
// register first and tolerate 403 (registration closed) and 400 (username/email already taken);
// in both cases the account already exists and we simply log in.
// IMPORTANT: register MUST come before login. Logging in with a not-yet-created account would
// count as a failed login and trip the IP rate limiter, which then blocks all auth requests.
async function ensureLoggedIn(page) {
  const regResp = await page.request.post('/api/v1/auth/register', {
    data: {
      username: TEST_USERNAME,
      email: `${TEST_USERNAME}@test.com`,
      password: TEST_PASSWORD,
    },
  })
  expect([200, 400, 403]).toContain(regResp.status())

  const loginResp = await page.request.post('/api/v1/auth/login', {
    data: { username: TEST_USERNAME, password: TEST_PASSWORD },
  })
  expect(loginResp.status()).toBe(200)
  const loginData = await loginResp.json()
  const token = loginData?.data?.device_token
  expect(token).toBeTruthy()
  return token
}

export async function loginAsNewUser(page) {
  const token = await ensureLoggedIn(page)

  // UI login so the SPA stores session state (route guard) and the server sets the auth cookie.
  await page.goto('/login')
  await page.getByPlaceholder('请输入用户名').fill(TEST_USERNAME)
  await page.getByPlaceholder('请输入密码').fill(TEST_PASSWORD)
  await page.getByRole('button', { name: '登录' }).click()
  await page.waitForURL(/\/dashboard/)

  // Attach the device token as a Bearer header to every request from this context.
  // This authenticates page.request.* API calls (and the SPA's own axios calls) without
  // relying on cookie/SameSite/proxy behavior, which was causing 401s on API requests.
  await page.context().setExtraHTTPHeaders({ Authorization: `Bearer ${token}` })

  const devicesResp = await page.request.get('/api/v1/devices')
  const devices = await devicesResp.json()
  const deviceId = devices.data?.[0]?.id

  return { username: TEST_USERNAME, deviceId }
}
