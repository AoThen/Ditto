import { defineStore } from 'pinia'
import { ref } from 'vue'

export const useUserStore = defineStore('user', () => {
  const token = ref(localStorage.getItem('token') || '')
  const userInfo = ref(JSON.parse(localStorage.getItem('userInfo') || 'null'))

  function setToken(newToken) {
    token.value = newToken
    localStorage.setItem('token', newToken)
  }

  function setRefreshToken(newRefreshToken) {
    localStorage.setItem('refresh_token', newRefreshToken)
  }

  function setUserInfo(info) {
    userInfo.value = info
    if (info) {
      localStorage.setItem('userInfo', JSON.stringify(info))
    }
  }

  function logout() {
    token.value = ''
    userInfo.value = null
    localStorage.removeItem('token')
    localStorage.removeItem('refresh_token')
    localStorage.removeItem('userInfo')
    // Trigger WS disconnect event
    window.dispatchEvent(new CustomEvent('ws-disconnect'))
  }

  return {
    token,
    userInfo,
    setToken,
    setRefreshToken,
    setUserInfo,
    logout,
  }
})
