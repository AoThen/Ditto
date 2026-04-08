import axios from 'axios'
import { ElMessage } from 'element-plus'
import router from '@/router'

const request = axios.create({
  baseURL: import.meta.env.VITE_API_BASE_URL || 'http://localhost:8080',
  timeout: 10000,
})

// Track whether a token refresh is in progress
let isRefreshing = false
// Queue of failed requests that will be retried after token refresh
let failedQueue = []

function processQueue(error, token = null) {
  failedQueue.forEach((prom) => {
    if (error) {
      prom.reject(error)
    } else {
      prom.resolve(token)
    }
  })
  failedQueue = []
}

// Request interceptor: attach Authorization header
request.interceptors.request.use(
  (config) => {
    const token = localStorage.getItem('token')
    if (token) {
      config.headers.Authorization = `Bearer ${token}`
    }
    return config
  },
  (error) => {
    return Promise.reject(error)
  }
)

// Response interceptor: handle errors
request.interceptors.response.use(
  (response) => {
    return response.data
  },
  async (error) => {
    const originalRequest = error.config
    const backendMessage = error.response?.data?.message

    if (error.response) {
      const { status } = error.response

      // 401: Token expired or invalid — attempt refresh
      if (status === 401 && !originalRequest._retry) {
        if (isRefreshing) {
          // Another request is already refreshing, queue this one
          return new Promise((resolve, reject) => {
            failedQueue.push({ resolve, reject })
          })
            .then((token) => {
              originalRequest.headers.Authorization = `Bearer ${token}`
              return request(originalRequest)
            })
            .catch((err) => {
              return Promise.reject(err)
            })
        }

        originalRequest._retry = true
        isRefreshing = true

        const refreshToken = localStorage.getItem('refresh_token')
        if (!refreshToken) {
          // No refresh token available, force logout
          localStorage.removeItem('token')
          localStorage.removeItem('refresh_token')
          localStorage.removeItem('userInfo')
          router.push('/login')
          return Promise.reject(new Error(backendMessage || '登录已过期，请重新登录'))
        }

        try {
          // Call refresh token endpoint
          const refreshResponse = await axios.post(
            `${request.defaults.baseURL}/api/v1/auth/refresh`,
            { refresh_token: refreshToken }
          )
          const newToken = refreshResponse.data?.data?.token
          const newRefreshToken = refreshResponse.data?.data?.refresh_token

          if (!newToken) {
            throw new Error('No token in refresh response')
          }

          localStorage.setItem('token', newToken)
          if (newRefreshToken) {
            localStorage.setItem('refresh_token', newRefreshToken)
          }

          // Retry all queued requests with the new token
          processQueue(null, newToken)

          // Retry the original request
          originalRequest.headers.Authorization = `Bearer ${newToken}`
          return request(originalRequest)
        } catch (refreshError) {
          // Refresh failed — force logout
          processQueue(refreshError, null)
          localStorage.removeItem('token')
          localStorage.removeItem('refresh_token')
          localStorage.removeItem('userInfo')
          router.push('/login')
          return Promise.reject(new Error(backendMessage || '登录已过期，请重新登录'))
        } finally {
          isRefreshing = false
        }
      }

      // 403: Forbidden
      if (status === 403) {
        ElMessage.error(backendMessage || '没有权限访问')
      }
      // 429: Rate limited
      else if (status === 429) {
        ElMessage.error(backendMessage || '请求过于频繁，请稍后重试')
      }
      // 500: Server error
      else if (status === 500) {
        ElMessage.error(backendMessage || '服务器内部错误')
      }
      // Other errors
      else {
        ElMessage.error(backendMessage || '请求失败')
      }
    } else if (error.request) {
      ElMessage.error('网络错误，请检查连接')
    } else {
      ElMessage.error(error.message || '未知错误')
    }
    return Promise.reject(new Error(backendMessage || error.message || '请求失败'))
  }
)

export default request
