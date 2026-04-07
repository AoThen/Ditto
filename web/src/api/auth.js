import request from './request'
import { useUserStore } from '@/stores/user'

export function login(data) {
  if (import.meta.env.VITE_MOCK === 'true') {
    return new Promise((resolve) => {
      setTimeout(() => {
        resolve({
          code: 0,
          data: {
            device_token: 'mock-token-' + Date.now(),
            device_id: 'mock-device',
          },
          message: '登录成功',
        })
      }, 500)
    })
  }
  return request.post('/api/v1/auth/login', data)
}

export function register(data) {
  if (import.meta.env.VITE_MOCK === 'true') {
    return new Promise((resolve) => {
      setTimeout(() => {
        resolve({
          code: 0,
          data: null,
          message: '注册成功',
        })
      }, 500)
    })
  }
  return request.post('/api/v1/auth/register', data)
}

/**
 * Helper: after login/register resolves, persist token and userInfo to the store.
 */
export function handleLoginResponse(res) {
  const userStore = useUserStore()
  // Backend returns code: 0 for success
  if (res.code === 0) {
    const token = res.data?.device_token || res.data?.token
    const deviceInfo = res.data?.device_id ? { device_id: res.data.device_id } : null
    userStore.setToken(token)
    if (deviceInfo) {
      userStore.setUserInfo(deviceInfo)
    }
  }
}
