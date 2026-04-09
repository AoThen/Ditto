import request from './request'

export function login(data) {
  if (import.meta.env.VITE_MOCK === 'true') {
    return new Promise((resolve) => {
      setTimeout(() => {
        resolve({
          code: 0,
          data: { device_id: 'mock-device' },
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
