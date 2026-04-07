// Test the full registration -> login -> logout flow
import { test, expect } from '@playwright/test'

test.describe('Authentication', () => {
  test('should register a new user successfully', async ({ page }) => {
    // Go to registration page
    await page.goto('/register')

    // Fill form
    const timestamp = Date.now()
    await page.getByPlaceholder('请输入用户名').fill(`e2e_user_${timestamp}`)
    await page.getByPlaceholder('请输入邮箱地址').fill(`e2e_${timestamp}@test.com`)
    await page.getByPlaceholder('请输入密码').fill('testpass123')
    await page.getByPlaceholder('请再次输入密码').fill('testpass123')

    // Submit
    await page.getByRole('button', { name: '注册' }).click()

    // Should redirect to login
    await expect(page).toHaveURL('/login')
  })

  test('should login with valid credentials', async ({ page }) => {
    // First register
    const timestamp = Date.now()
    const username = `login_user_${timestamp}`
    const email = `login_${timestamp}@test.com`

    await page.goto('/register')
    await page.getByPlaceholder('请输入用户名').fill(username)
    await page.getByPlaceholder('请输入邮箱地址').fill(email)
    await page.getByPlaceholder('请输入密码').fill('testpass123')
    await page.getByPlaceholder('请再次输入密码').fill('testpass123')
    await page.getByRole('button', { name: '注册' }).click()
    await expect(page).toHaveURL('/login')

    // Now login
    await page.getByPlaceholder('请输入用户名').fill(username)
    await page.getByPlaceholder('请输入密码').fill('testpass123')
    await page.getByRole('button', { name: '登录' }).click()

    // Should redirect to dashboard
    await expect(page).toHaveURL(/\/dashboard/)

    // Should see dashboard elements
    await expect(page.getByText('Ditto Cloud')).toBeVisible()
  })

  test('should show error for wrong password', async ({ page }) => {
    await page.goto('/login')
    await page.getByPlaceholder('请输入用户名').fill('nonexistent_user_xyz')
    await page.getByPlaceholder('请输入密码').fill('wrongpassword')
    await page.getByRole('button', { name: '登录' }).click()

    // Should stay on login page (not redirect to dashboard)
    await page.waitForTimeout(2000)
    await expect(page).toHaveURL('/login')
  })

  test('should logout successfully', async ({ page }) => {
    // Login first (reuse the flow)
    const timestamp = Date.now()
    const username = `logout_user_${timestamp}`

    await page.goto('/register')
    await page.getByPlaceholder('请输入用户名').fill(username)
    await page.getByPlaceholder('请输入邮箱地址').fill(`${username}@test.com`)
    await page.getByPlaceholder('请输入密码').fill('testpass123')
    await page.getByPlaceholder('请再次输入密码').fill('testpass123')
    await page.getByRole('button', { name: '注册' }).click()
    await page.waitForURL('/login', { timeout: 10000 })

    await page.getByPlaceholder('请输入用户名').fill(username)
    await page.getByPlaceholder('请输入密码').fill('testpass123')
    await page.getByRole('button', { name: '登录' }).click()
    await page.waitForURL(/\/dashboard/, { timeout: 10000 })

    // Click logout
    await page.getByRole('button', { name: '退出登录' }).click()

    // Should redirect to login
    await expect(page).toHaveURL('/login')
  })

  test('should validate password match on registration', async ({ page }) => {
    await page.goto('/register')
    await page.getByPlaceholder('请输入密码').fill('password123')
    await page.getByPlaceholder('请再次输入密码').fill('different_password')

    // Submit - Element Plus form validation will catch this before submit
    // Click elsewhere to trigger validation
    await page.getByLabel('用户名').click()

    // Should show validation error (password mismatch)
    await expect(page.getByText('两次输入的密码不一致')).toBeVisible({ timeout: 5000 })
  })
})
