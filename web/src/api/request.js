import axios from 'axios'
import { ElMessage } from 'element-plus'
import router from '@/router'
import { useUserStore } from '@/stores/user'

const request = axios.create({
  baseURL: import.meta.env.VITE_API_BASE_URL || 'http://localhost:8080',
  timeout: 10000,
  // HIGH FIX (H1): Send cookies with requests (HttpOnly tokens)
  withCredentials: true,
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

// MEDIUM FIX (M2): Clear failedQueue on route navigation
router.beforeEach((_to, _from, next) => {
  if (isRefreshing && failedQueue.length > 0) {
    processQueue(new Error('Navigation cancelled refresh'), null)
    isRefreshing = false
  }
  next()
})

// Request interceptor: cookies are sent automatically (withCredentials: true)
// No need to manually attach Authorization header (H1)
request.interceptors.request.use(
  (config) => {
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
            .then(() => {
              return request(originalRequest)
            })
            .catch((err) => {
              return Promise.reject(err)
            })
        }

        originalRequest._retry = true
        isRefreshing = true

        try {
          // H1: Call refresh token endpoint (cookies sent automatically)
          const refreshResponse = await axios.post(
            `${request.defaults.baseURL}/api/v1/auth/refresh`,
            {},
            { withCredentials: true }
          )

          // H1: Backend sets new HttpOnly cookies, no need to store tokens
          const userStore = useUserStore()
          // Check if we got a valid response (cookies are set by backend)
          if (refreshResponse.status === 200) {
            // Retry all queued requests (cookies handle auth)
            processQueue(null, null)
            // Retry the original request
            return request(originalRequest)
          }

          throw new Error('Token refresh failed')
        } catch (refreshError) {
          // Refresh failed — force logout
          processQueue(refreshError, null)
          const userStore = useUserStore()
          userStore.logout()
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

/**
 * downloadBlob - Helper for downloading files with proper token refresh handling.
 * Unlike the main request interceptor (which returns response.data),
 * this returns the full response so the caller can access the blob.
 *
 * Usage:
 *   const blob = await downloadBlob('/clips/123/download', { params: { format_type: 'text' } })
 */
export async function downloadBlob(url, options = {}) {
  const { params = {}, responseType = 'blob' } = options

  try {
    const response = await request.get(url, {
      params,
      responseType,
      // Return full response object (not just data)
      validateStatus: (status) => status < 500,
    })

    // If we got a 401, the interceptor should have handled refresh
    if (response.status === 401) {
      // Refresh failed or user not authenticated
      const userStore = useUserStore()
      userStore.logout()
      router.push('/login')
      throw new Error('登录已过期，请重新登录')
    }

    return response.data
  } catch (error) {
    // If it's already a handled error from the interceptor, re-throw
    if (error.message && error.message.includes('登录已过期')) {
      throw error
    }
    // Otherwise let the interceptor handle it and still throw
    throw error
  }
}
