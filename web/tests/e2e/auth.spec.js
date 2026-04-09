// Test the full registration -> login -> logout flow
import { test, expect } from '@playwright/test'

test.describe('Authentication', () => {
  test('should register a new user successfully', async ({ page }) => {
    await page.goto('/register')

    const timestamp = Date.now()
    await page.getByPlaceholder('请输入用户名').fill(`e2e_user_${timestamp}`)
    await page.getByPlaceholder('请输入邮箱地址').fill(`e2e_${timestamp}@test.com`)
    await page.getByPlaceholder('请输入密码').fill('testpass123')
    await page.getByPlaceholder('请再次输入密码').fill('testpass123')

    await page.getByRole('button', { name: '注册' }).click()

    await expect(page).toHaveURL('/login')
  })

  test('should login with valid credentials', async ({ page }) => {
    const timestamp = Date.now()
    const username = `login_user_${timestamp}`

    await page.goto('/register')
    await page.getByPlaceholder('请输入用户名').fill(username)
    await page.getByPlaceholder('请输入邮箱地址').fill(`login_${timestamp}@test.com`)
    await page.getByPlaceholder('请输入密码').fill('testpass123')
    await page.getByPlaceholder('请再次输入密码').fill('testpass123')
    await page.getByRole('button', { name: '注册' }).click()
    await expect(page).toHaveURL('/login')

    await page.getByPlaceholder('请输入用户名').fill(username)
    await page.getByPlaceholder('请输入密码').fill('testpass123')
    await page.getByRole('button', { name: '登录' }).click()

    await expect(page).toHaveURL(/\/dashboard/)
    await expect(page.getByText('Ditto Cloud')).toBeVisible()
  })

  test('should show error for wrong password', async ({ page }) => {
    await page.goto('/login')
    await page.getByPlaceholder('请输入用户名').fill('nonexistent_user_xyz')
    await page.getByPlaceholder('请输入密码').fill('wrongpassword')
    await page.getByRole('button', { name: '登录' }).click()

    await page.waitForTimeout(2000)
    await expect(page).toHaveURL('/login')
  })

  test('should logout successfully', async ({ page }) => {
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

    await page.getByRole('button', { name: '退出登录' }).click()

    await expect(page).toHaveURL('/login')
  })

  test('should validate password match on registration', async ({ page }) => {
    await page.goto('/register')
    await page.getByPlaceholder('请输入密码').fill('password123')
    await page.getByPlaceholder('请再次输入密码').fill('different_password')

    await page.getByLabel('用户名').click()

    await expect(page.getByText('两次输入的密码不一致')).toBeVisible({ timeout: 5000 })
  })
})
