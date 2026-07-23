import { test, expect } from '@playwright/test'
import { loginAsNewUser } from './helpers'

test.describe('Authentication', () => {
  test('should register a new user successfully', async ({ page }) => {
    await loginAsNewUser(page)
    await expect(page).toHaveURL(/\/dashboard/)
  })

  test('should login with valid credentials', async ({ page }) => {
    await loginAsNewUser(page)
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
    await loginAsNewUser(page)

    await page.getByRole('button', { name: '退出登录' }).click()

    await expect(page).toHaveURL('/login')
  })
})