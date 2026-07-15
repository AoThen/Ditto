import { defineStore } from 'pinia'
import { ref } from 'vue'

export const useUserStore = defineStore('user', () => {
  const isLoggedIn = ref(false)
  const deviceId = ref('')
  const username = ref('')
  const role = ref('')

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
      } catch {
        // ignore
      }
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
    document.cookie = 'device_id=; max-age=0; path=/'
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
    setUserInfo,
    logout,
  }
})