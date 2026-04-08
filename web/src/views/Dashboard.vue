<template>
  <div class="dashboard-layout">
    <!-- Top Bar -->
    <header class="topbar">
      <div class="topbar-title">Ditto Cloud</div>
      <div class="topbar-user">
        <span v-if="userStore.userInfo" class="username">{{ userStore.userInfo.username }}</span>
        <el-button type="danger" size="small" @click="handleLogout">退出登录</el-button>
      </div>
    </header>

    <div class="main-wrapper">
      <!-- Sidebar Menu -->
      <aside class="sidebar">
        <el-menu
          :default-active="activeMenu"
          router
          class="sidebar-menu"
        >
          <el-menu-item index="/dashboard">
            <el-icon><DataAnalysis /></el-icon>
            <span>仪表盘</span>
          </el-menu-item>
          <el-menu-item index="/dashboard/clips">
            <el-icon><Document /></el-icon>
            <span>剪贴板</span>
          </el-menu-item>
          <el-menu-item index="/dashboard/groups">
            <el-icon><FolderOpened /></el-icon>
            <span>分组管理</span>
          </el-menu-item>
          <el-menu-item index="/dashboard/devices">
            <el-icon><Monitor /></el-icon>
            <span>设备管理</span>
          </el-menu-item>
          <el-menu-item index="/dashboard/settings">
            <el-icon><Setting /></el-icon>
            <span>设置</span>
          </el-menu-item>
        </el-menu>
      </aside>

      <!-- Main Content Area -->
      <main class="main-content">
        <router-view />
      </main>
    </div>
  </div>
</template>

<script setup>
import { computed } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useUserStore } from '../stores/user'
import { useWebSocket } from '@/composables/useWebSocket'
import { ElMessage } from 'element-plus'
import { DataAnalysis, Document, Monitor, Setting, FolderOpened } from '@element-plus/icons-vue'

const route = useRoute()
const router = useRouter()
const userStore = useUserStore()
const ws = useWebSocket()

const activeMenu = computed(() => route.path)

function handleLogout() {
  ws.disconnect()
  userStore.logout()
  ElMessage.success('已退出登录')
  router.push('/login')
}
</script>

<style scoped>
.dashboard-layout {
  height: 100vh;
  display: flex;
  flex-direction: column;
}

.topbar {
  height: 56px;
  background: #409eff;
  color: #fff;
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 20px;
  box-shadow: 0 2px 4px rgba(0, 0, 0, 0.1);
  flex-shrink: 0;
}

.topbar-title {
  font-size: 20px;
  font-weight: 600;
}

.topbar-user {
  display: flex;
  align-items: center;
  gap: 12px;
}

.username {
  font-size: 14px;
}

.main-wrapper {
  display: flex;
  flex: 1;
  overflow: hidden;
}

.sidebar {
  width: 200px;
  background: #fff;
  border-right: 1px solid #e4e7ed;
  flex-shrink: 0;
  overflow-y: auto;
}

.sidebar-menu {
  border-right: none;
}

.main-content {
  flex: 1;
  background: #f5f7fa;
  overflow-y: auto;
}
</style>
