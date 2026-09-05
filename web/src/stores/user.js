import { defineStore } from 'pinia'
import { ref } from 'vue'

export const useUserStore = defineStore('user', () => {
  const isLoggedIn = ref(false)
  const deviceId = ref('')
  const username = ref('')
  const role = ref('')

  // Restores the locally cached view of the session. Cheap and synchronous so
  // the router guard and the WebSocket connector can rely on it, but it cannot
  // tell whether the HttpOnly access cookie is still valid — verifySession does.
  function checkAuthState() {
    const deviceIdCookie = getCookie('device_id')
    if (deviceIdCookie) {
      isLoggedIn.value = true
      deviceId.value = deviceIdCookie
    }
    const storedUser = localStorage.getItem('userInfo')
    if (storedUser) {
      try {
        const parsed = JSON.parse(storedUser)
        username.value = parsed?.username || ''
        role.value = parsed?.role || ''
        deviceId.value = parsed?.device_id || ''
        isLoggedIn.value = true
      } catch {
        // ignore
      }
    }
  }

  // Confirms the session against the server. The access token is short-lived,
  // so a page load long after login must not assume it is still alive.
  // A rejected call means the response interceptor already forced a logout
  // (unrecoverable 401) or the API is unreachable — in the latter case the
  // session is left alone so a reload can recover.
  async function verifySession() {
    if (!isLoggedIn.value) return false
    // Imported lazily: api/auth pulls in the axios instance and the router, and
    // the store is loaded by the router guards themselves.
    const { getMe } = await import('@/api/auth')
    try {
      const res = await getMe()
      if (res?.code !== 0) {
        logout()
        return false
      }
      if (res.data) {
        deviceId.value = res.data.device_id || deviceId.value
        username.value = res.data.username || username.value
        role.value = res.data.role || role.value
        localStorage.setItem('userInfo', JSON.stringify({
          device_id: deviceId.value,
          username: username.value,
          role: role.value,
        }))
      }
      return true
    } catch {
      return false
    }
  }

  function setUserInfo(info) {
    if (info) {
      deviceId.value = info.device_id || ''
      username.value = info.username || ''
      role.value = info.role || ''
      localStorage.setItem('userInfo', JSON.stringify(info))
      isLoggedIn.value = true
    }
  }

  function logout() {
    isLoggedIn.value = false
    deviceId.value = ''
    username.value = ''
    role.value = ''
    localStorage.removeItem('userInfo')
    document.cookie = 'device_id=; expires=Thu, 01 Jan 1970 00:00:00 GMT; path=/'
    window.dispatchEvent(new CustomEvent('ws-disconnect'))
  }

  function getCookie(name) {
    const match = document.cookie.match(new RegExp('(^| )' + name + '=([^;]+)'))
    return match ? decodeURIComponent(match[2]) : ''
  }

  return {
    isLoggedIn,
    deviceId,
    username,
    role,
    checkAuthState,
    verifySession,
    setUserInfo,
    logout,
  }
})