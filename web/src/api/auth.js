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

// Session probe: a locally stored "logged in" flag says nothing about whether
// the short-lived access cookie is still valid. The tight timeout keeps a cold
// start snappy when the API is unreachable.
export function getMe() {
  if (import.meta.env.VITE_MOCK === 'true') {
    return Promise.resolve({ code: 0, data: null })
  }
  return request({ url: '/api/v1/auth/me', method: 'get', timeout: 3000 })
}
