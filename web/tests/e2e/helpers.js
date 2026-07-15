// Shared E2E test helpers — register via API, login via UI
import { test, expect } from '@playwright/test'

// Fixed credentials so authentication is idempotent across spec files and test retries.
// The server only allows the first user to register (RegisterAllowed), so generating a
// brand-new random user per call would fail with 403 on every retry / second registration.
const TEST_USERNAME = process.env.E2E_USERNAME || 'e2e_test_user'
const TEST_PASSWORD = process.env.E2E_PASSWORD || 'e2e_test_pass_123'

// Per-test unique counter so every test authenticates on an ISOLATED device.
// The SPA never sends X-Device-Name, so without this all tests share one device
// ("dev-1-"). The server bumps a device's TokenVersion on logout/refresh, while Login
// always issues a token with version 0 — so once one test logs out, every later login
// (version 0) is rejected (401) and the SPA self-logs-out, breaking all later UI tests.
let deviceSeq = 0

export async function loginAsNewUser(page) {
  // Idempotent registration: register once; tolerate 403 (registration closed) and
  // 400 (username/email already taken) and reuse the existing account.
  const regResp = await page.request.post('/api/v1/auth/register', {
    data: {
      username: TEST_USERNAME,
      email: `${TEST_USERNAME}@test.com`,
      password: TEST_PASSWORD,
    },
  })
  expect([200, 400, 403]).toContain(regResp.status())

  // Unique device for this test (used by BOTH the SPA login and the API login below),
  // so the logout/refresh of one test can never invalidate another test's token.
  deviceSeq += 1
  const deviceName = `e2e-${deviceSeq}-${Date.now()}`

  // Intercept the SPA's own login request and inject our isolated device name.
  // (The SPA does not send X-Device-Name itself.)
  await page.route('**/api/v1/auth/login', async (route) => {
    const headers = { ...route.request().headers() }
    delete headers['content-length']
    headers['X-Device-Name'] = deviceName
    await route.continue({ headers })
  })

  // UI login FIRST so the SPA stores session state (route guard) and the server sets the
  // auth cookie. This MUST happen before any API login: the API login response also sets the
  // auth cookie, and if that cookie were present when we navigate to /login, the SPA would
  // auto-redirect /login -> /dashboard and the login form would never render.
  await page.goto('/login')
  await page.getByPlaceholder('请输入用户名').fill(TEST_USERNAME)
  await page.getByPlaceholder('请输入密码').fill(TEST_PASSWORD)
  await page.getByRole('button', { name: '登录' }).click()
  await page.waitForURL(/\/dashboard/)

  await page.unroute('**/api/v1/auth/login')

  // Obtain a device token via the API on the SAME isolated device and attach it as a Bearer
  // header to every request from this context. This authenticates page.request.* API calls
  // without depending solely on cookie/SameSite/proxy behavior (which caused 401s).
  const loginResp = await page.request.post('/api/v1/auth/login', {
    data: { username: TEST_USERNAME, password: TEST_PASSWORD },
    headers: { 'X-Device-Name': deviceName },
  })
  expect(loginResp.status()).toBe(200)
  const loginData = await loginResp.json()
  const token = loginData?.data?.device_token
  expect(token).toBeTruthy()
  await page.context().setExtraHTTPHeaders({ Authorization: `Bearer ${token}` })

  const deviceId = loginData?.data?.device_id

  return { username: TEST_USERNAME, deviceId }
}
