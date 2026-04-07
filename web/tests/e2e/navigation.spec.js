// Test navigation between pages
import { test, expect } from '@playwright/test'

async function loginAsNewUser(page) {
  const timestamp = Date.now()
  const username = `nav_user_${timestamp}`

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
}

test.describe('Navigation', () => {
  test('should redirect to login when not authenticated', async ({ page }) => {
    await page.goto('/dashboard/clips')
    await expect(page).toHaveURL('/login')
  })

  test('should navigate between clips and settings', async ({ page }) => {
    await loginAsNewUser(page)

    // Should start on clips page (default redirect from /dashboard)
    await expect(page).toHaveURL(/\/dashboard\/clips/)

    // Click settings in sidebar (el-menu-item with text '设置')
    await page.getByRole('menuitem', { name: '设置' }).click()
    await expect(page).toHaveURL(/\/dashboard\/settings/)

    // Click clips in sidebar (el-menu-item with text '剪贴板')
    await page.getByRole('menuitem', { name: '剪贴板' }).click()
    await expect(page).toHaveURL(/\/dashboard\/clips/)
  })

  test('should preserve login on page reload', async ({ page }) => {
    await loginAsNewUser(page)
    await expect(page).toHaveURL(/\/dashboard/)

    // Reload
    await page.reload()

    // Should still be on dashboard
    await expect(page).toHaveURL(/\/dashboard/)
  })
})
