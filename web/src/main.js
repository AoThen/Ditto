import { createApp } from 'vue'
import ElementPlus from 'element-plus'
import zhCn from 'element-plus/dist/locale/zh-cn.mjs'
import 'element-plus/dist/index.css'
import { createPinia } from 'pinia'
import router from './router'
import App from './App.vue'
import './style.css'

const app = createApp(App)

import { Document, CirclePlus, Monitor, Coin, Refresh, Download, ArrowDown, Folder, DataAnalysis, Setting, FolderOpened, User, Search } from '@element-plus/icons-vue'

const icons = [Document, CirclePlus, Monitor, Coin, Refresh, Download, ArrowDown, Folder, DataAnalysis, Setting, FolderOpened, User, Search]
for (const component of icons) {
  app.component(component.name, component)
}

app.use(createPinia())
app.use(router)
app.use(ElementPlus, { locale: zhCn })

// H1: Check auth state from HttpOnly cookies on app init
import { useUserStore } from './stores/user'
const userStore = useUserStore()
userStore.checkAuthState()

// MEDIUM FIX (M1): Global Vue error handler for render/template errors
app.config.errorHandler = async (err, instance, info) => {
  console.error('[Vue Error]', info, err)
  try {
    const { ElMessage } = await import('element-plus')
    ElMessage.error('页面渲染异常: ' + (err.message || '未知错误'))
  } catch (e) {
    // element-plus might be unavailable during crashes
  }
}

// Global unhandled promise rejection handler
window.addEventListener('unhandledrejection', async (event) => {
  console.error('[Unhandled Rejection]', event.reason)
  event.preventDefault()
  try {
    const { ElMessage } = await import('element-plus')
    ElMessage.error('请求异常: ' + (event.reason?.message || '未知错误'))
  } catch (e) {
    // element-plus might be unavailable
  }
})

app.mount('#app')
