import { defineStore } from 'pinia'
import { ref } from 'vue'

// HIGH FIX (H1): Token is now stored in HttpOnly cookies (set by backend).
// The store no longer holds token/refreshToken - it only holds display data.
// This prevents XSS-based token theft since JS cannot access HttpOnly cookies.

export const useUserStore = defineStore('user', () => {
  const isLoggedIn = ref(false)
  const deviceId = ref('')
  const username = ref('')

  // Read cookie-based auth state from non-HttpOnly device_id cookie
  // or from localStorage fallback (for backward compat during migration)
  function checkAuthState() {
    const deviceIdCookie = getCookie('device_id')
    if (deviceIdCookie) {
      isLoggedIn.value = true
      deviceId.value = deviceIdCookie
    }
    // Username from localStorage (non-sensitive display data)
    const storedUser = localStorage.getItem('userInfo')
    if (storedUser) {
      try {
        const parsed = JSON.parse(storedUser)
        username.value = parsed?.username || ''
      } catch {
        // ignore
      }
    }
  }

  function setUserInfo(info) {
    if (info) {
      username.value = info.username || ''
      localStorage.setItem('userInfo', JSON.stringify(info))
      isLoggedIn.value = true
    }
  }

  function logout() {
    isLoggedIn.value = false
    deviceId.value = ''
    username.value = ''
    localStorage.removeItem('userInfo')
    // Trigger WS disconnect event
    window.dispatchEvent(new CustomEvent('ws-disconnect'))
    // Notify backend to clear cookies (POST /auth/logout)
  }

  // Helper to read non-HttpOnly cookies
  function getCookie(name) {
    const match = document.cookie.match(new RegExp('(^| )' + name + '=([^;]+)'))
    return match ? decodeURIComponent(match[2]) : ''
  }

  return {
    isLoggedIn,
    deviceId,
    username,
    checkAuthState,
    setUserInfo,
    logout,
  }
})
