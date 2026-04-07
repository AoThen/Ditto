import axios from 'axios'

const request = axios.create({
  baseURL: import.meta.env.VITE_API_BASE_URL || 'http://localhost:8080',
  timeout: 10000,
})

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
  (error) => {
    if (error.response) {
      const { status } = error.response
      if (status === 401) {
        localStorage.removeItem('token')
        window.location.href = '/login'
      } else if (status === 403) {
        ElMessage && ElMessage.error('没有权限访问')
      } else if (status === 500) {
        ElMessage && ElMessage.error('服务器内部错误')
      } else {
        ElMessage && ElMessage.error(error.response.data?.message || '请求失败')
      }
    } else if (error.request) {
      ElMessage && ElMessage.error('网络错误，请检查连接')
    } else {
      ElMessage && ElMessage.error(error.message || '未知错误')
    }
    return Promise.reject(error)
  }
)

export default request
