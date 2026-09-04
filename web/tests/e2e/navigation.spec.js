import { test, expect } from '@playwright/test'
import { loginAsNewUser } from './helpers'

test.describe('Navigation', () => {
  test('should redirect to login when not authenticated', async ({ page }) => {
    await page.goto('/dashboard/clips')
    await expect(page).toHaveURL('/login')
  })

  test('should navigate between clips and settings', async ({ page, request }) => {
    await loginAsNewUser(page, request)

    await expect(page).toHaveURL(/\/dashboard/)

    await page.getByRole('menuitem', { name: '设置' }).click()
    await expect(page).toHaveURL(/\/dashboard\/settings/)

    await page.getByRole('menuitem', { name: '剪贴板' }).click()
    await expect(page).toHaveURL(/\/dashboard\/clips/)
  })

  test('should preserve login on page reload', async ({ page, request }) => {
    await loginAsNewUser(page, request)
    await expect(page).toHaveURL(/\/dashboard/)

    await page.reload()

    await expect(page).toHaveURL(/\/dashboard/)
  })
})